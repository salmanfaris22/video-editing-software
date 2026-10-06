#pragma once

namespace lectern::mobile {

/// The single-file phone page (phone_page.html) inlined into the binary, so the
/// desktop can host it with no assets to install.
[[nodiscard]] const char* phonePageHtml() noexcept;

}  // namespace lectern::mobile