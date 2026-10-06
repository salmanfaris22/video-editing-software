#include "editor/ColorGrading.h"

#include <QByteArray>
#include <QList>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace lectern::editor {

namespace {

constexpr Chromaticity kD65{0.3127, 0.3290};

// ---- .cube parsing ---------------------------------------------------------------

/// Locale-independent number parsing (Qt's QByteArray::toDouble uses the C locale).
bool parseNumber(const QByteArray& token, double& out) {
    bool ok = false;
    out = token.toDouble(&ok);
    return ok && std::isfinite(out);
}

/// A per-channel 1D LUT as a 33³ 3D LUT over the same input domain.
Lut3D expand1d(const std::vector<float>& values, int size, const std::array<float, 3>& lo, const std::array<float, 3>& hi) {
    Lut3D lut;
    lut.size = 33;
    lut.domainMin = lo;
    lut.domainMax = hi;
    lut.table.resize(static_cast<std::size_t>(33 * 33 * 33 * 3));
    // Grid point k of the 3D table is the fraction k/32 of the domain, i.e.
    // the same fraction of the 1D table.
    auto channel = [&](float fraction, int c) {
        const float t = std::clamp(fraction, 0.0f, 1.0f) * static_cast<float>(size - 1);
        const int i = std::min(static_cast<int>(t), size - 2);
        const float f = t - static_cast<float>(i);
        const float a = values[static_cast<std::size_t>(3 * i + c)];
        const float b = values[static_cast<std::size_t>(3 * (i + 1) + c)];
        return a + (b - a) * f;
    };
    std::size_t k = 0;
    for (int b = 0; b < 33; ++b) {
        for (int g = 0; g < 33; ++g) {
            for (int r = 0; r < 33; ++r) {
                lut.table[k++] = channel(static_cast<float>(r) / 32.0f, 0);
                lut.table[k++] = channel(static_cast<float>(g) / 32.0f, 1);
                lut.table[k++] = channel(static_cast<float>(b) / 32.0f, 2);
            }
        }
    }
    return lut;
}

// ---- Built-in log conversions ------------------------------------------------------

struct LogSpec {
    const char* id;
    const char* name;
    const char* curve;
    const char* gamut;
};

constexpr LogSpec kLogSpecs[] = {
    {"builtin:apple-log", "Apple Log → Rec.709", "apple-log", "rec2020"},
    {"builtin:s-log3", "Sony S-Log3 / S-Gamut3.Cine → Rec.709", "s-log3", "s-gamut3.cine"},
    {"builtin:v-log", "Panasonic V-Log → Rec.709", "v-log", "v-gamut"},
    {"builtin:logc3", "ARRI LogC3 → Rec.709", "logc3", "awg3"},
};

/// Scene-linear → display-encoded Rec.709 with a filmic shoulder
/// (Narkowicz's ACES fit) and BT.1886 (2.4) display encoding.
double toneMap(double x) {
    x = std::max(0.0, x) * 0.6;
    const double y = std::clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
    return std::pow(y, 1.0 / 2.4);
}

Lut3D generateLogLut(const LogSpec& spec) {
    constexpr int kSize = 33;
    const Matrix3 m = gamutToRec709(spec.gamut);
    Lut3D lut;
    lut.size = kSize;
    lut.title = spec.name;
    lut.table.resize(static_cast<std::size_t>(kSize * kSize * kSize * 3));
    std::array<double, kSize> linear{};
    for (int i = 0; i < kSize; ++i) linear[static_cast<std::size_t>(i)] = decodeLog(spec.curve, i / double(kSize - 1));
    std::size_t k = 0;
    for (int b = 0; b < kSize; ++b) {
        for (int g = 0; g < kSize; ++g) {
            for (int r = 0; r < kSize; ++r) {
                const std::array<double, 3> in{linear[static_cast<std::size_t>(r)], linear[static_cast<std::size_t>(g)],
                                               linear[static_cast<std::size_t>(b)]};
                for (std::size_t c = 0; c < 3; ++c) {
                    const double v = m[c][0] * in[0] + m[c][1] * in[1] + m[c][2] * in[2];
                    lut.table[k++] = static_cast<float>(toneMap(v));
                }
            }
        }
    }
    return lut;
}

}  // namespace

// ---- Lut3D -------------------------------------------------------------------------

std::array<float, 3> Lut3D::sample(float r, float g, float b) const noexcept {
    const int n = size;
    if (n < 2 || table.size() < static_cast<std::size_t>(n) * n * n * 3) return {r, g, b};
    auto coord = [&](float v, std::size_t axis, int& i0, float& f) {
        const float lo = domainMin[axis];
        const float hi = domainMax[axis];
        float t = hi > lo ? (v - lo) / (hi - lo) : 0.0f;
        t = std::clamp(t, 0.0f, 1.0f) * static_cast<float>(n - 1);
        i0 = std::min(static_cast<int>(t), n - 2);
        f = t - static_cast<float>(i0);
    };
    int ri = 0;
    int gi = 0;
    int bi = 0;
    float rf = 0;
    float gf = 0;
    float bf = 0;
    coord(r, 0, ri, rf);
    coord(g, 1, gi, gf);
    coord(b, 2, bi, bf);
    auto at = [&](int x, int y, int z) {
        return &table[3 * ((static_cast<std::size_t>(z) * static_cast<std::size_t>(n) + static_cast<std::size_t>(y)) *
                               static_cast<std::size_t>(n) +
                           static_cast<std::size_t>(x))];
    };
    const float* c000 = at(ri, gi, bi);
    const float* c100 = at(ri + 1, gi, bi);
    const float* c010 = at(ri, gi + 1, bi);
    const float* c110 = at(ri + 1, gi + 1, bi);
    const float* c001 = at(ri, gi, bi + 1);
    const float* c101 = at(ri + 1, gi, bi + 1);
    const float* c011 = at(ri, gi + 1, bi + 1);
    const float* c111 = at(ri + 1, gi + 1, bi + 1);
    std::array<float, 3> out{};
    for (std::size_t c = 0; c < 3; ++c) {
        const float x00 = c000[c] + (c100[c] - c000[c]) * rf;
        const float x10 = c010[c] + (c110[c] - c010[c]) * rf;
        const float x01 = c001[c] + (c101[c] - c001[c]) * rf;
        const float x11 = c011[c] + (c111[c] - c011[c]) * rf;
        const float y0 = x00 + (x10 - x00) * gf;
        const float y1 = x01 + (x11 - x01) * gf;
        out[c] = y0 + (y1 - y0) * bf;
    }
    return out;
}

Result<Lut3D> parseCubeLut(std::string_view text) {
    Lut3D lut;
    int size3 = 0;
    int size1 = 0;
    std::vector<float> values;
    std::size_t pos = 0;
    int lineNo = 0;
    while (pos <= text.size()) {
        const std::size_t end = std::min(text.find('\n', pos), text.size());
        QByteArray line = QByteArray(text.data() + pos, static_cast<qsizetype>(end - pos)).trimmed();
        pos = end + 1;
        ++lineNo;
        if (line.isEmpty() || line.startsWith('#')) {
            if (end == text.size()) break;
            continue;
        }
        const QList<QByteArray> tokens = line.simplified().split(' ');
        const QByteArray key = tokens.front().toUpper();
        if (key == "TITLE") {
            lut.title = line.mid(5).trimmed().replace("\"", "").toStdString();
        } else if (key == "LUT_3D_SIZE" || key == "LUT_1D_SIZE") {
            double v = 0;
            if (tokens.size() < 2 || !parseNumber(tokens[1], v)) return fail(ErrorCode::ParseError, "invalid " + key.toStdString());
            (key == "LUT_3D_SIZE" ? size3 : size1) = static_cast<int>(v);
        } else if (key == "DOMAIN_MIN" || key == "DOMAIN_MAX") {
            if (tokens.size() < 4) return fail(ErrorCode::ParseError, "invalid " + key.toStdString());
            auto& target = key == "DOMAIN_MIN" ? lut.domainMin : lut.domainMax;
            for (std::size_t i = 0; i < 3; ++i) {
                double v = 0;
                if (!parseNumber(tokens[static_cast<qsizetype>(i + 1)], v)) return fail(ErrorCode::ParseError, "invalid domain");
                target[i] = static_cast<float>(v);
            }
        } else if (key == "LUT_3D_INPUT_RANGE" || key == "LUT_1D_INPUT_RANGE") {
            double lo = 0;
            double hi = 1;
            if (tokens.size() < 3 || !parseNumber(tokens[1], lo) || !parseNumber(tokens[2], hi)) {
                return fail(ErrorCode::ParseError, "invalid input range");
            }
            lut.domainMin = {static_cast<float>(lo), static_cast<float>(lo), static_cast<float>(lo)};
            lut.domainMax = {static_cast<float>(hi), static_cast<float>(hi), static_cast<float>(hi)};
        } else if (std::isalpha(static_cast<unsigned char>(key.front()))) {
            // Other keywords (e.g. LUT_IN_VIDEO_RANGE) are ignored.
        } else {
            if (tokens.size() < 3) return fail(ErrorCode::ParseError, "line " + std::to_string(lineNo) + ": expected 3 numbers");
            for (int i = 0; i < 3; ++i) {
                double v = 0;
                if (!parseNumber(tokens[i], v)) {
                    return fail(ErrorCode::ParseError, "line " + std::to_string(lineNo) + ": not a number");
                }
                values.push_back(static_cast<float>(v));
            }
        }
        if (end == text.size()) break;
    }
    for (std::size_t i = 0; i < 3; ++i) {
        if (!(lut.domainMax[i] > lut.domainMin[i])) return fail(ErrorCode::ParseError, "empty LUT domain");
    }
    if (size3 > 0) {
        if (size3 < 2 || size3 > 256) return fail(ErrorCode::Unsupported, "LUT size " + std::to_string(size3) + " is not supported");
        const std::size_t want = static_cast<std::size_t>(size3) * size3 * size3 * 3;
        if (values.size() != want) {
            return fail(ErrorCode::ParseError, "LUT has " + std::to_string(values.size() / 3) + " entries, expected " +
                                                   std::to_string(want / 3));
        }
        lut.size = size3;
        lut.table = std::move(values);
        return lut;
    }
    if (size1 > 0) {
        if (size1 < 2 || size1 > 65536 || values.size() != static_cast<std::size_t>(size1) * 3) {
            return fail(ErrorCode::ParseError, "invalid 1D LUT");
        }
        Lut3D expanded = expand1d(values, size1, lut.domainMin, lut.domainMax);
        expanded.title = lut.title;
        return expanded;
    }
    return fail(ErrorCode::ParseError, "not a .cube LUT (no LUT_3D_SIZE)");
}

Result<Lut3D> loadCubeLut(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return fail(ErrorCode::NotFound, "cannot open " + path.string());
    std::ostringstream text;
    text << in.rdbuf();
    const std::string data = text.str();
    if (data.size() > 64u * 1024 * 1024) return fail(ErrorCode::Unsupported, "LUT file is too large");
    return parseCubeLut(data);
}

const std::vector<BuiltinLut>& builtinLuts() {
    static const std::vector<BuiltinLut> all = [] {
        std::vector<BuiltinLut> v;
        for (const LogSpec& s : kLogSpecs) v.push_back({s.id, s.name});
        return v;
    }();
    return all;
}

std::shared_ptr<const Lut3D> builtinLut(std::string_view id) {
    static std::mutex mutex;
    static std::map<std::string, std::shared_ptr<const Lut3D>, std::less<>> cache;
    std::lock_guard lock(mutex);
    if (const auto it = cache.find(id); it != cache.end()) return it->second;
    for (const LogSpec& s : kLogSpecs) {
        if (id == s.id) {
            auto lut = std::make_shared<const Lut3D>(generateLogLut(s));
            cache.emplace(std::string(id), lut);
            return lut;
        }
    }
    return nullptr;
}

std::shared_ptr<const Lut3D> LutCache::get(const std::string& ref, const std::filesystem::path& projectDir) {
    if (ref.empty()) return nullptr;
    if (ref.starts_with("builtin:")) return builtinLut(ref);
    std::filesystem::path path = std::filesystem::path(ref);
    if (path.is_relative()) path = projectDir / path;
    const std::string key = path.lexically_normal().string();
    {
        std::lock_guard lock(mutex_);
        if (const auto it = luts_.find(key); it != luts_.end()) return it->second;
    }
    auto loaded = loadCubeLut(path);
    std::shared_ptr<const Lut3D> lut = loaded ? std::make_shared<const Lut3D>(std::move(*loaded)) : nullptr;
    std::lock_guard lock(mutex_);
    if (luts_.size() > 16) luts_.clear();
    luts_[key] = lut;  // failures are remembered too (no re-reading every frame)
    return lut;
}

// ---- Colorimetry -------------------------------------------------------------------

Matrix3 multiply(const Matrix3& a, const Matrix3& b) {
    Matrix3 m{};
    for (std::size_t i = 0; i < 3; ++i) {
        for (std::size_t j = 0; j < 3; ++j) {
            for (std::size_t k = 0; k < 3; ++k) m[i][j] += a[i][k] * b[k][j];
        }
    }
    return m;
}

Matrix3 inverse(const Matrix3& m) {
    const double a = m[0][0], b = m[0][1], c = m[0][2];
    const double d = m[1][0], e = m[1][1], f = m[1][2];
    const double g = m[2][0], h = m[2][1], i = m[2][2];
    const double det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (std::abs(det) < 1e-12) return {{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
    const double s = 1.0 / det;
    return {{{(e * i - f * h) * s, (c * h - b * i) * s, (b * f - c * e) * s},
             {(f * g - d * i) * s, (a * i - c * g) * s, (c * d - a * f) * s},
             {(d * h - e * g) * s, (b * g - a * h) * s, (a * e - b * d) * s}}};
}

Matrix3 rgbToXyz(Chromaticity r, Chromaticity g, Chromaticity b, Chromaticity w) {
    auto xyz = [](Chromaticity c) { return std::array<double, 3>{c.x / c.y, 1.0, (1.0 - c.x - c.y) / c.y}; };
    const auto R = xyz(r);
    const auto G = xyz(g);
    const auto B = xyz(b);
    const auto W = xyz(w);
    const Matrix3 p{{{R[0], G[0], B[0]}, {R[1], G[1], B[1]}, {R[2], G[2], B[2]}}};
    const Matrix3 pi = inverse(p);
    std::array<double, 3> s{};
    for (std::size_t i = 0; i < 3; ++i) s[i] = pi[i][0] * W[0] + pi[i][1] * W[1] + pi[i][2] * W[2];
    Matrix3 m{};
    for (std::size_t i = 0; i < 3; ++i) {
        for (std::size_t j = 0; j < 3; ++j) m[i][j] = p[i][j] * s[j];
    }
    return m;
}

Matrix3 gamutToRec709(std::string_view gamut) {
    static const Matrix3 rec709 = rgbToXyz({0.640, 0.330}, {0.300, 0.600}, {0.150, 0.060}, kD65);
    Matrix3 source;
    if (gamut == "rec2020") {
        source = rgbToXyz({0.708, 0.292}, {0.170, 0.797}, {0.131, 0.046}, kD65);
    } else if (gamut == "s-gamut3.cine") {
        source = rgbToXyz({0.766, 0.275}, {0.225, 0.800}, {0.089, -0.087}, kD65);
    } else if (gamut == "v-gamut") {
        source = rgbToXyz({0.730, 0.280}, {0.165, 0.840}, {0.100, -0.030}, kD65);
    } else if (gamut == "awg3") {
        source = rgbToXyz({0.6840, 0.3130}, {0.2210, 0.8480}, {0.0861, -0.1020}, kD65);
    } else {
        return {{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
    }
    return multiply(inverse(rec709), source);
}

double decodeLog(std::string_view curve, double v) {
    if (curve == "apple-log") {
        // Apple Log Profile white paper (2023).
        constexpr double R0 = -0.05641088, Rt = 0.01, c = 47.28711236, beta = 0.00964052, gamma = 0.08550479,
                         delta = 0.69336945;
        const double Pt = c * (Rt - R0) * (Rt - R0);
        if (v >= Pt) return std::pow(2.0, (v - delta) / gamma) - beta;
        if (v > 0) return std::sqrt(v / c) + R0;
        return R0;
    }
    if (curve == "s-log3") {
        // Sony S-Log3 technical summary.
        if (v >= 171.2102946929 / 1023.0) return std::pow(10.0, (v * 1023.0 - 420.0) / 261.5) * (0.18 + 0.01) - 0.01;
        return (v * 1023.0 - 95.0) * 0.01125000 / (171.2102946929 - 95.0);
    }
    if (curve == "v-log") {
        // Panasonic V-Log/V-Gamut reference manual.
        constexpr double b = 0.00873, c = 0.241514, d = 0.598206;
        if (v < 0.181) return (v - 0.125) / 5.6;
        return std::pow(10.0, (v - d) / c) - b;
    }
    if (curve == "logc3") {
        // ARRI LogC3, EI 800.
        constexpr double a = 5.555556, b = 0.052272, c = 0.247190, d = 0.385537, e = 5.367655, f = 0.092809;
        if (v > e * 0.010591 + f) return (std::pow(10.0, (v - d) / c) - b) / a;
        return (v - f) / e;
    }
    return v;
}

// ---- Grading -----------------------------------------------------------------------

std::array<double, 3> wheelChroma(const ColorParams::Wheel& w) {
    // The puck moves on the vectorscope plane: x → Cb, y → Cr (BT.709).
    const double cb = std::clamp(w.x, -1.0, 1.0) * 0.15;
    const double cr = std::clamp(w.y, -1.0, 1.0) * 0.15;
    return {1.5748 * cr, -0.1873 * cb - 0.4681 * cr, 1.8556 * cb};
}

Matrix3 gamutToBt709(Primaries primaries) {
    constexpr Chromaticity d65{0.3127, 0.3290};
    const Matrix3 bt709 = rgbToXyz({0.640, 0.330}, {0.300, 0.600}, {0.150, 0.060}, d65);
    Matrix3 source = bt709;
    if (primaries == Primaries::DisplayP3) source = rgbToXyz({0.680, 0.320}, {0.265, 0.690}, {0.150, 0.060}, d65);
    if (primaries == Primaries::Bt2020) source = rgbToXyz({0.708, 0.292}, {0.170, 0.797}, {0.131, 0.046}, d65);
    return multiply(inverse(bt709), source);
}

namespace {

double srgbToLinear(double v) { return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); }

/// SMPTE ST 2084 (PQ) code value → nits.
double pqToNits(double e) {
    constexpr double m1 = 2610.0 / 16384.0;
    constexpr double m2 = 2523.0 / 4096.0 * 128.0;
    constexpr double c1 = 3424.0 / 4096.0;
    constexpr double c2 = 2413.0 / 4096.0 * 32.0;
    constexpr double c3 = 2392.0 / 4096.0 * 32.0;
    const double p = std::pow(std::clamp(e, 0.0, 1.0), 1.0 / m2);
    return 10000.0 * std::pow(std::max(p - c1, 0.0) / (c2 - c3 * p), 1.0 / m1);
}

/// ARIB STD-B67 (HLG) code value → scene light (0…1).
double hlgToScene(double e) {
    constexpr double a = 0.17883277;
    constexpr double b = 1.0 - 4.0 * a;
    const double c = 0.5 - a * std::log(4.0 * a);
    e = std::clamp(e, 0.0, 1.0);
    return e <= 0.5 ? e * e / 3.0 : (std::exp((e - c) / a) + b) / 12.0;
}

/// Highlight roll-off: identity below the knee, then approaches 1 smoothly (slope 1 at the knee).
double highlightRolloff(double x) {
    if (x <= kToneKnee) return x;
    return kToneKnee + (1.0 - kToneKnee) * (1.0 - std::exp(-(x - kToneKnee) / (1.0 - kToneKnee)));
}

}  // namespace

std::array<double, 3> convertInputColor(const InputColor& in, double r, double g, double b) {
    if (in.isIdentity()) return {r, g, b};
    std::array<double, 3> lin{};
    const std::array<double, 3> code{r, g, b};
    const bool hdr = in.transfer == Transfer::Pq || in.transfer == Transfer::Hlg;
    if (in.transfer == Transfer::Pq) {
        for (int i = 0; i < 3; ++i) lin[static_cast<std::size_t>(i)] = pqToNits(code[static_cast<std::size_t>(i)]) / kHdrReferenceWhite;
    } else if (in.transfer == Transfer::Hlg) {
        // Scene light → display light for a 1000-nit display (system gamma 1.2, BT.2100).
        std::array<double, 3> scene{};
        for (int i = 0; i < 3; ++i) scene[static_cast<std::size_t>(i)] = hlgToScene(code[static_cast<std::size_t>(i)]);
        const double ys = 0.2627 * scene[0] + 0.6780 * scene[1] + 0.0593 * scene[2];
        const double ootf = ys > 0 ? std::pow(ys, 0.2) : 0.0;
        for (int i = 0; i < 3; ++i) lin[static_cast<std::size_t>(i)] = 1000.0 * ootf * scene[static_cast<std::size_t>(i)] / kHdrReferenceWhite;
    } else if (in.transfer == Transfer::Srgb) {
        for (int i = 0; i < 3; ++i) lin[static_cast<std::size_t>(i)] = srgbToLinear(code[static_cast<std::size_t>(i)]);
    } else {
        for (int i = 0; i < 3; ++i) lin[static_cast<std::size_t>(i)] = std::pow(std::max(code[static_cast<std::size_t>(i)], 0.0), 2.4);
    }
    const Matrix3 m = gamutToBt709(in.primaries);
    std::array<double, 3> out{};
    for (int i = 0; i < 3; ++i) {
        const auto& row = m[static_cast<std::size_t>(i)];
        out[static_cast<std::size_t>(i)] = std::max(0.0, row[0] * lin[0] + row[1] * lin[1] + row[2] * lin[2]);
    }
    if (hdr) {  // compress highlights by the largest channel, keeping hue
        const double peak = std::max({out[0], out[1], out[2]});
        if (peak > kToneKnee) {
            const double scale = highlightRolloff(peak) / peak;
            for (double& v : out) v *= scale;
        }
    }
    for (double& v : out) v = std::pow(std::clamp(v, 0.0, 1.0), 1.0 / 2.4);  // BT.1886 display encoding
    return out;
}

void applyInputColor(QImage& img, const InputColor& input) {
    if (img.isNull()) return;
    if (input.isIdentity()) {
        if (img.format() != QImage::Format_RGB32 && img.format() != QImage::Format_ARGB32_Premultiplied) {
            img = img.convertToFormat(img.hasAlphaChannel() ? QImage::Format_ARGB32_Premultiplied : QImage::Format_RGB32);
        }
        return;
    }
    // Read at 16 bits per channel so 10-bit sources keep their precision.
    const QImage src = img.convertToFormat(QImage::Format_RGBA64);
    QImage out(src.size(), QImage::Format_RGB32);
    for (int y = 0; y < src.height(); ++y) {
        const auto* in = reinterpret_cast<const QRgba64*>(src.constScanLine(y));
        auto* px = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < src.width(); ++x) {
            const auto c = convertInputColor(input, in[x].red() / 65535.0, in[x].green() / 65535.0, in[x].blue() / 65535.0);
            px[x] = qRgb(static_cast<int>(std::lround(c[0] * 255)), static_cast<int>(std::lround(c[1] * 255)),
                         static_cast<int>(std::lround(c[2] * 255)));
        }
    }
    img = std::move(out);
}

std::array<double, 3> boostAndHue(const ColorParams& c, double r, double g, double b) {
    if (c.colorBoost != 0) {  // vibrance: muted colors gain more saturation than vivid ones
        const double l = 0.2126 * r + 0.7152 * g + 0.0722 * b;
        const double chroma = std::max({r, g, b}) - std::min({r, g, b});
        const double s = 1.0 + c.colorBoost * (1.0 - chroma);
        r = l + (r - l) * s;
        g = l + (g - l) * s;
        b = l + (b - l) * s;
    }
    if (c.hue != 0) {  // rotate around the gray axis, keeping luma (Rec.709 weights)
        const double a = c.hue * 3.14159265358979323846;
        const double cs = std::cos(a);
        const double sn = std::sin(a);
        const double nr = (0.2126 + 0.7874 * cs - 0.2126 * sn) * r + (0.7152 - 0.7152 * cs - 0.7152 * sn) * g +
                          (0.0722 - 0.0722 * cs + 0.9278 * sn) * b;
        const double ng = (0.2126 - 0.2126 * cs + 0.143 * sn) * r + (0.7152 + 0.2848 * cs + 0.140 * sn) * g +
                          (0.0722 - 0.0722 * cs - 0.283 * sn) * b;
        const double nb = (0.2126 - 0.2126 * cs - 0.7874 * sn) * r + (0.7152 - 0.7152 * cs + 0.7152 * sn) * g +
                          (0.0722 + 0.9278 * cs + 0.0722 * sn) * b;
        r = nr;
        g = ng;
        b = nb;
    }
    return {std::clamp(r, 0.0, 1.0), std::clamp(g, 0.0, 1.0), std::clamp(b, 0.0, 1.0)};
}

ColorCurves colorCurves(const ColorParams& c) {
    const double gain = std::pow(2.0, c.exposure);
    const double contrast = 1.0 + c.contrast;
    const std::array<double, 3> channelGain = {1.0 + 0.15 * c.temperature + 0.05 * c.tint, 1.0 - 0.10 * c.tint,
                                               1.0 - 0.15 * c.temperature + 0.05 * c.tint};
    const auto liftC = wheelChroma(c.lift);
    const auto gammaC = wheelChroma(c.gamma);
    const auto gainC = wheelChroma(c.gain);
    const auto offsetC = wheelChroma(c.offset);
    ColorCurves curve{};
    for (std::size_t ch = 0; ch < 3; ++ch) {
        const double lift = 0.25 * c.lift.master + liftC[ch];
        const double exponent = std::pow(2.0, -(c.gamma.master + 2.0 * gammaC[ch]));
        const double highlights = 1.0 + 0.5 * c.gain.master + 2.0 * gainC[ch];
        for (int v = 0; v < 256; ++v) {
            double x = v / 255.0 * gain + c.brightness * 0.3;
            x = (x - c.pivot) * contrast + c.pivot;
            // Shadows / highlights: lift or pull only the darks / brights.
            const double t = std::clamp(x, 0.0, 1.0);
            x += 0.35 * c.shadows * (1.0 - t) * (1.0 - t) * (1.0 - t);
            x += 0.35 * c.highlights * t * t * t;
            x *= channelGain[ch];
            x = x + lift * (1.0 - x);
            if (x > 0) x = std::pow(x, exponent);
            x *= highlights;
            x += 0.25 * c.offset.master + offsetC[ch];  // Offset moves the whole signal
            curve[ch][static_cast<std::size_t>(v)] = static_cast<std::uint8_t>(std::clamp(std::lround(x * 255.0), 0L, 255L));
        }
    }
    return curve;
}

void applyColor(QImage& img, const ColorParams& c, const Lut3D* lut) {
    const bool useLut = lut && lut->size >= 2 && c.lutAmount > 0.0;
    if (img.isNull() || (!useLut && c.curvesAreIdentity())) return;
    if (img.format() != QImage::Format_RGB32 && img.format() != QImage::Format_ARGB32_Premultiplied) {
        img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    }

    // Per-channel curves (everything but the LUT and saturation) as tables.
    const ColorCurves curve = colorCurves(c);
    const double sat = 1.0 + c.saturation;
    const bool pixelOps = c.colorBoost != 0 || c.hue != 0;
    const float mix = static_cast<float>(std::clamp(c.lutAmount, 0.0, 1.0));

    for (int y = 0; y < img.height(); ++y) {
        auto* px = reinterpret_cast<std::uint32_t*>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const std::uint32_t v = px[x];
            const std::uint32_t a = v >> 24;
            if (a == 0) continue;
            int r = static_cast<int>((v >> 16) & 0xFF);
            int g = static_cast<int>((v >> 8) & 0xFF);
            int b = static_cast<int>(v & 0xFF);
            if (a < 255) {  // un-premultiply
                r = std::min(255, r * 255 / static_cast<int>(a));
                g = std::min(255, g * 255 / static_cast<int>(a));
                b = std::min(255, b * 255 / static_cast<int>(a));
            }
            if (useLut) {
                const float rf = static_cast<float>(r) / 255.0f;
                const float gf = static_cast<float>(g) / 255.0f;
                const float bf = static_cast<float>(b) / 255.0f;
                const auto out = lut->sample(rf, gf, bf);
                auto to8 = [mix](float graded, float original) {
                    const float m = original + (graded - original) * mix;
                    return static_cast<int>(std::clamp(std::lround(m * 255.0f), 0L, 255L));
                };
                r = to8(out[0], rf);
                g = to8(out[1], gf);
                b = to8(out[2], bf);
            }
            r = curve[0][static_cast<std::size_t>(r)];
            g = curve[1][static_cast<std::size_t>(g)];
            b = curve[2][static_cast<std::size_t>(b)];
            if (sat != 1.0) {
                const double l = 0.2126 * r + 0.7152 * g + 0.0722 * b;
                r = static_cast<int>(std::clamp(l + (r - l) * sat, 0.0, 255.0));
                g = static_cast<int>(std::clamp(l + (g - l) * sat, 0.0, 255.0));
                b = static_cast<int>(std::clamp(l + (b - l) * sat, 0.0, 255.0));
            }
            if (pixelOps) {
                const auto o = boostAndHue(c, r / 255.0, g / 255.0, b / 255.0);
                r = static_cast<int>(std::lround(o[0] * 255.0));
                g = static_cast<int>(std::lround(o[1] * 255.0));
                b = static_cast<int>(std::lround(o[2] * 255.0));
            }
            if (a < 255) {
                r = r * static_cast<int>(a) / 255;
                g = g * static_cast<int>(a) / 255;
                b = b * static_cast<int>(a) / 255;
            }
            px[x] = (a << 24) | (static_cast<std::uint32_t>(r) << 16) | (static_cast<std::uint32_t>(g) << 8) |
                    static_cast<std::uint32_t>(b);
        }
    }
}

}  // namespace lectern::editor
