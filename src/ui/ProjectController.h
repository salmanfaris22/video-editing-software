#pragma once

#include "project/Project.h"
#include "timeline/EditOps.h"
#include "ui/SerialExecutor.h"

#include <QObject>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <vector>

namespace lectern::ui {

/// Editor view-model and the single entry point for edits from QML.
///
/// Every edit runs on the UI thread against the document, goes through the
/// tested engine functions (timeline::edit, Subtitles, …), lands in a bounded
/// undo history (consecutive slider moves merge into one step) and is saved
/// transactionally on a worker thread shortly after. QML only reads the view
/// properties and calls these methods. Implementation is split by concern:
/// ProjectController.cpp (document, views), ProjectEdits.cpp (timeline),
/// ProjectLooks.cpp (setup, layout, style, text, effects, audio, imports).
class ProjectController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by AppController")

    // Document
    Q_PROPERTY(bool loaded READ loaded NOTIFY projectChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QString title READ title NOTIFY projectChanged)
    Q_PROPERTY(QString path READ path NOTIFY projectChanged)
    Q_PROPERTY(int canvasWidth READ canvasWidth NOTIFY projectChanged)
    Q_PROPERTY(int canvasHeight READ canvasHeight NOTIFY projectChanged)
    Q_PROPERTY(double frameRate READ frameRate NOTIFY projectChanged)
    Q_PROPERTY(QString frameRateText READ frameRateText NOTIFY projectChanged)
    Q_PROPERTY(QString aspect READ aspect NOTIFY projectChanged)
    Q_PROPERTY(QString backgroundColor READ backgroundColor NOTIFY projectChanged)
    Q_PROPERTY(QString backgroundColor2 READ backgroundColor2 NOTIFY projectChanged)
    Q_PROPERTY(double duration READ duration NOTIFY projectChanged)
    Q_PROPERTY(QString notice READ notice NOTIFY projectChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY editStateChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY editStateChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY editStateChanged)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)

    // Views
    Q_PROPERTY(QVariantList tracks READ tracks NOTIFY projectChanged)
    Q_PROPERTY(QVariantList markers READ markers NOTIFY projectChanged)
    Q_PROPERTY(QVariantList layoutRegions READ layoutRegions NOTIFY projectChanged)
    Q_PROPERTY(QVariantList layoutPresets READ layoutPresets NOTIFY projectChanged)
    Q_PROPERTY(QString layoutPreset READ layoutPreset NOTIFY projectChanged)
    Q_PROPERTY(QVariantList subtitles READ subtitles NOTIFY projectChanged)
    Q_PROPERTY(QVariantMap style READ style NOTIFY projectChanged)
    Q_PROPERTY(QVariantMap roleTransforms READ roleTransforms NOTIFY projectChanged)
    Q_PROPERTY(QVariantList textOverlays READ textOverlays NOTIFY projectChanged)
    Q_PROPERTY(QString screenMedia READ screenMedia NOTIFY projectChanged)
    Q_PROPERTY(QString cameraMedia READ cameraMedia NOTIFY projectChanged)
    Q_PROPERTY(QVariantList textAnimations READ textAnimations CONSTANT)
    Q_PROPERTY(QVariantList builtinLuts READ builtinLuts CONSTANT)
    /// Background blur can separate people from their background on this system.
    Q_PROPERTY(bool segmentationAvailable READ segmentationAvailable CONSTANT)

    // Selection (the clip the inspector panels edit)
    Q_PROPERTY(QString selectedClip READ selectedClip NOTIFY selectionChanged)
    Q_PROPERTY(QStringList selectedClips READ selectedClips NOTIFY selectionChanged)
    /// The layer (track) the timeline highlights: picked from its header or by selecting one of its clips.
    Q_PROPERTY(QString selectedTrack READ selectedTrack NOTIFY selectionChanged)
    /// "track" = one clip at a time, gap delete · "allTracks" = linked recording on every track
    Q_PROPERTY(QString linkedEditMode READ linkedEditMode WRITE setLinkedEditMode NOTIFY linkedEditModeChanged)
    Q_PROPERTY(QVariantMap selection READ selection NOTIFY selectionChanged)

    // Background jobs
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString busyText READ busyText NOTIFY busyChanged)
    Q_PROPERTY(QVariantList silences READ silences NOTIFY silencesChanged)
    Q_PROPERTY(double silenceTotal READ silenceTotal NOTIFY silencesChanged)

public:
    explicit ProjectController(QObject* parent = nullptr);
    ~ProjectController() override;

    // ---- Document --------------------------------------------------------
    /// Loads <dir>/project.json asynchronously; emits opened() or failed().
    Q_INVOKABLE void open(const QString& dir);
    Q_INVOKABLE void close();
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void save();
    /// Saves now and waits until the file is written (tests, quitting).
    void flushSaves();
    /// "M:SS" / "H:MM:SS" for labels.
    Q_INVOKABLE QString formatTime(double seconds) const;
    /// "M:SS.ff" with frames, for the transport.
    Q_INVOKABLE QString formatTimecode(double seconds) const;

    // ---- Selection -------------------------------------------------------
    Q_INVOKABLE void selectClip(const QString& clipId, bool additive = false);
    Q_INVOKABLE void selectClips(const QStringList& clipIds);
    Q_INVOKABLE void selectAllClips();
    Q_INVOKABLE bool isClipSelected(const QString& clipId) const;
    Q_INVOKABLE void clearSelection();
    /// Selects the topmost clip under `seconds` on the given track (or any track when empty).
    Q_INVOKABLE void selectAt(double seconds, const QString& trackId = {});

    // ---- Cut (ProjectEdits.cpp) -----------------------------------------
    /// Splits the selected clip (and its linked recording) at `seconds`, or
    /// every recording track when nothing is selected.
    Q_INVOKABLE void splitAt(double seconds);
    /// Deletes the selection (behavior depends on `linkedEditMode`).
    Q_INVOKABLE void deleteSelected();
    Q_INVOKABLE void deleteClip(const QString& clipId);
    /// Ripple-removes [start, end) from every unlocked track.
    Q_INVOKABLE void removeRange(double start, double end);
    Q_INVOKABLE void trimClip(const QString& clipId, const QString& edge, double seconds);
    Q_INVOKABLE void moveClip(const QString& clipId, double seconds);

    // ---- Layers (ProjectTracks.cpp) ----------------------------------------
    Q_INVOKABLE void selectTrack(const QString& trackId);
    /// Adds an empty layer: kind "overlay" (text, images, video on top), "audio" or "subtitle"; selects it.
    Q_INVOKABLE QString addTrack(const QString& kind);
    Q_INVOKABLE void renameTrack(const QString& trackId, const QString& name);
    /// Moves a layer one step up (+1) or down (−1) among layers of its kind (stacking order).
    Q_INVOKABLE void moveTrack(const QString& trackId, int direction);
    Q_INVOKABLE bool canMoveTrack(const QString& trackId, int direction) const;
    Q_INVOKABLE void deleteTrack(const QString& trackId);
    /// Moves a free clip onto another layer of the same kind, starting near `seconds`.
    Q_INVOKABLE void moveClipToTrack(const QString& clipId, const QString& trackId, double seconds);
    Q_INVOKABLE void addMarker(double seconds, const QString& label = {});
    Q_INVOKABLE void removeMarker(const QString& markerId);
    /// Finds pauses in the narration (async; results in `silences`).
    Q_INVOKABLE void findSilences(double thresholdDb, double minPauseSeconds, double paddingSeconds);
    Q_INVOKABLE void removeSilences();
    Q_INVOKABLE void clearSilences();

    // ---- Setup & layout (ProjectLooks.cpp) -------------------------------
    Q_INVOKABLE void setTitle(const QString& title);
    Q_INVOKABLE void setCanvasAspect(const QString& aspect);
    Q_INVOKABLE void setFrameRate(int fps);
    Q_INVOKABLE void setBackground(const QString& color, const QString& color2);
    /// One layout for the whole video.
    Q_INVOKABLE void setLayoutPreset(const QString& preset);
    /// Layout from `seconds` until the next layout change.
    Q_INVOKABLE void setLayoutFrom(const QString& preset, double seconds);
    Q_INVOKABLE QString layoutAt(double seconds) const;
    /// Moves/scales every screen or camera clip relative to its layout slot.
    Q_INVOKABLE void setRoleTransform(const QString& role, double x, double y, double scale);

    // ---- Style ------------------------------------------------------------
    /// Keys: screenPadding, screenRadius, screenShadow, cameraShape, cameraBorder,
    /// cameraBorderColor, cameraMirror, subtitleSize, subtitleColor,
    /// subtitleBackground, subtitlePosition.
    Q_INVOKABLE void setStyleValue(const QString& key, const QVariant& value);

    // ---- Text ---------------------------------------------------------------
    /// Adds a text clip at `start` with a preset ("title", "lower-third", "caption", "callout"); selects it.
    Q_INVOKABLE void addText(const QString& text, double start, double duration = 4.0, const QString& preset = QStringLiteral("title"));
    Q_INVOKABLE void setText(const QString& clipId, const QString& text);
    /// Keys: preset, size, weight, color, background, alignment, animationIn,
    /// animationOut (see textAnimations), inDuration, outDuration (seconds).
    Q_INVOKABLE void setTextValue(const QString& clipId, const QString& key, const QVariant& value);

    // ---- Clips (overlay, transform, effects, color, audio) -----------------
    Q_INVOKABLE void setClipPosition(const QString& clipId, double x, double y);
    Q_INVOKABLE void setClipScale(const QString& clipId, double scale);
    Q_INVOKABLE void setClipOpacity(const QString& clipId, double opacity);
    /// Evaluated transform at a timeline time (for keyframe editing at the playhead).
    Q_INVOKABLE QVariantMap clipTransformAt(const QString& clipId, double timelineSec) const;
    Q_INVOKABLE QVariantList clipKeyframeTimes(const QString& clipId, const QString& property) const;
    /// `keyframe`: when true and `timelineSec` is on the clip, writes a key; otherwise sets the constant value.
    Q_INVOKABLE void setClipKeyframe(const QString& clipId, const QString& property, double timelineSec,
                                     const QVariant& value, bool keyframe = true);
    Q_INVOKABLE void removeClipKeyframe(const QString& clipId, const QString& property, double timelineSec);
    Q_INVOKABLE void clearClipKeyframes(const QString& clipId, const QString& property = {});
    Q_INVOKABLE void setClipEnabled(const QString& clipId, bool enabled);
    Q_INVOKABLE void setClipTiming(const QString& clipId, double start, double duration);
    /// type: "blur", "vignette", "zoom", "background-blur". Enabling adds the effect with defaults.
    Q_INVOKABLE void setEffectEnabled(const QString& clipId, const QString& type, bool enabled);
    Q_INVOKABLE void setEffectValue(const QString& clipId, const QString& type, const QString& param, double value);
    /// Copies the clip's "effects" or "color" to every clip of the same
    /// recording source (all screen or all camera segments) as one step.
    Q_INVOKABLE void applyToRole(const QString& clipId, const QString& what);

    // ---- Color (ProjectColor.cpp) ----------------------------------------------
    /// Keys: exposure, brightness, contrast, saturation, temperature, tint, lutAmount.
    Q_INVOKABLE void setColorValue(const QString& clipId, const QString& key, double value);
    Q_INVOKABLE void resetColor(const QString& clipId);
    /// Several adjustments as one undo step (color presets).
    Q_INVOKABLE void setColorValues(const QString& clipId, const QVariantMap& values);
    /// wheel: "lift", "gamma", "gain"; (x, y) on the wheel, master: −1…1.
    Q_INVOKABLE void setColorWheel(const QString& clipId, const QString& wheel, double x, double y, double master);
    /// ref: a built-in conversion ("builtin:…"), a project LUT path, or "" for none.
    Q_INVOKABLE void setColorLut(const QString& clipId, const QString& ref);
    /// How the clip's source colors are read: "auto" (file tags), "rec709", "srgb",
    /// "display-p3", "rec2020", "rec2020-hlg", "rec2020-pq" (HDR is tone-mapped).
    Q_INVOKABLE void setInputColorSpace(const QString& clipId, const QString& space);
    /// Copies a .cube file into the project (media/luts) and applies it.
    Q_INVOKABLE void importLut(const QString& clipId, const QUrl& file);

    // ---- Canvas (ProjectCanvas.cpp) --------------------------------------------
    /// The topmost visible layer under a canvas point (fractions) at `seconds`: its clip id, or "".
    Q_INVOKABLE QString layerAt(double x, double y, double seconds) const;
    /// Where a clip is visible at `seconds`: {x, y, w, h} in canvas fractions,
    /// circle, role, aspect (width/height in pixels the corner handles keep;
    /// 0 = free), edges (edge handles allowed), vertical (moves vertically only).
    /// Empty when the clip is not visible then.
    Q_INVOKABLE QVariantMap layerBox(const QString& clipId, double seconds) const;
    /// Places a layer so that it is visible at {x, y, w, h}: screen and camera
    /// re-arrange the layout in effect (for this canvas aspect), overlays and
    /// text move and scale, subtitles move vertically and change size.
    Q_INVOKABLE void setLayerRect(const QString& clipId, double seconds, double x, double y, double w, double h);
    /// Shows a layer at {x, y, w, h} in the preview while it is dragged —
    /// no undo step, no view rebuild; setLayerRect commits it.
    Q_INVOKABLE void previewLayerRect(const QString& clipId, double seconds, double x, double y, double w, double h);
    /// Drops an uncommitted preview (the preview shows the document again).
    Q_INVOKABLE void cancelPreview();
    /// Back to the default place and size of the layer.
    Q_INVOKABLE void resetLayerRect(const QString& clipId, double seconds);
    /// The clip drawn as the "screen" or "camera" layer at `seconds`, or "".
    Q_INVOKABLE QString roleClipAt(const QString& role, double seconds) const;
    /// Resizes a layer about its center to `width` (canvas fraction), keeping its proportions.
    Q_INVOKABLE void setLayerWidth(const QString& clipId, double seconds, double width);
    /// Whether the layout in effect at `seconds` was arranged by hand (this canvas aspect).
    Q_INVOKABLE bool layoutCustomized(double seconds) const;
    /// Back to the preset arrangement of the layout in effect at `seconds`.
    Q_INVOKABLE void resetLayoutCustomization(double seconds);
    /// Keys: gainDb, muted, fadeIn, fadeOut.
    Q_INVOKABLE void setClipAudio(const QString& clipId, const QString& key, const QVariant& value);
    /// Keys: gainDb, muted, solo, locked, hidden.
    Q_INVOKABLE void setTrackValue(const QString& trackId, const QString& key, const QVariant& value);
    /// Copies a file into the project and adds it: purpose "overlay" (image or
    /// video on an overlay track at `seconds`) or "music" (audio track).
    Q_INVOKABLE void importMedia(const QUrl& file, const QString& purpose, double seconds);

    // ---- Subtitles ----------------------------------------------------------
    Q_INVOKABLE void addSubtitle(const QString& text, double start, double duration = 3.0);
    Q_INVOKABLE void setSubtitleText(const QString& clipId, const QString& text);
    Q_INVOKABLE void importSubtitles(const QUrl& file);
    /// Writes .srt or .vtt (by extension); returns false on failure (see `message`).
    Q_INVOKABLE bool exportSubtitles(const QUrl& file);
    Q_INVOKABLE void clearSubtitles();

    // ---- Property readers ---------------------------------------------------
    [[nodiscard]] bool loaded() const { return project_ != nullptr; }
    [[nodiscard]] bool loading() const { return loading_; }
    [[nodiscard]] QString title() const;
    [[nodiscard]] QString path() const { return QString::fromStdString(dir_.string()); }
    [[nodiscard]] int canvasWidth() const { return project_ ? project_->canvas.width : 1920; }
    [[nodiscard]] int canvasHeight() const { return project_ ? project_->canvas.height : 1080; }
    [[nodiscard]] double frameRate() const { return project_ ? project_->canvas.frameRate.toDouble() : 30.0; }
    [[nodiscard]] QString frameRateText() const;
    [[nodiscard]] QString aspect() const;
    [[nodiscard]] QString backgroundColor() const;
    [[nodiscard]] QString backgroundColor2() const;
    [[nodiscard]] double duration() const;
    [[nodiscard]] QString notice() const { return notice_; }
    [[nodiscard]] bool dirty() const { return dirty_; }
    [[nodiscard]] bool canUndo() const { return !undo_.empty(); }
    [[nodiscard]] bool canRedo() const { return !redo_.empty(); }
    [[nodiscard]] QString message() const { return message_; }
    [[nodiscard]] QVariantList tracks() const { return tracks_; }
    [[nodiscard]] QVariantList markers() const { return markers_; }
    [[nodiscard]] QVariantList layoutRegions() const { return layoutRegions_; }
    [[nodiscard]] QVariantList layoutPresets() const { return layoutPresets_; }
    [[nodiscard]] QString layoutPreset() const { return layoutPreset_; }
    [[nodiscard]] QVariantList subtitles() const { return subtitles_; }
    [[nodiscard]] QVariantMap style() const { return style_; }
    [[nodiscard]] QVariantMap roleTransforms() const { return roleTransforms_; }
    [[nodiscard]] QVariantList textOverlays() const { return textOverlays_; }
    [[nodiscard]] QString screenMedia() const { return screenMedia_; }
    [[nodiscard]] QString cameraMedia() const { return cameraMedia_; }
    [[nodiscard]] QVariantList textAnimations() const;
    [[nodiscard]] QVariantList builtinLuts() const;
    [[nodiscard]] bool segmentationAvailable() const;
    [[nodiscard]] QString selectedClip() const { return selectedClip_; }
    [[nodiscard]] QStringList selectedClips() const { return selectedClips_; }
    [[nodiscard]] QString selectedTrack() const { return selectedTrack_; }
    [[nodiscard]] QString linkedEditMode() const { return linkedEditMode_; }
    void setLinkedEditMode(const QString& mode);
    [[nodiscard]] QVariantMap selection() const { return selection_; }
    [[nodiscard]] bool busy() const { return !busyText_.isEmpty(); }
    [[nodiscard]] QString busyText() const { return busyText_; }
    [[nodiscard]] QVariantList silences() const;
    [[nodiscard]] double silenceTotal() const;

    /// Immutable copy of the current document (playback/export); null when closed.
    [[nodiscard]] std::shared_ptr<const project::Project> snapshot() const { return snapshot_; }
    [[nodiscard]] const std::filesystem::path& directory() const { return dir_; }

    /// Synchronous controller commands committed atomically as one assistant
    /// undo step. The callback must not load projects or start background jobs.
    QString assistantEdit(const QString& clientId, const QString& label, const std::function<void()>& edit);
    [[nodiscard]] QString undoLabel() const { return undo_.empty() ? QString() : undo_.back().label; }
    int undoAssistantEdits(const QString& clientId);

signals:
    void projectChanged();
    void loadingChanged();
    void opened(const QString& dir);
    void failed(const QString& message);
    void editStateChanged();
    void selectionChanged();
    void linkedEditModeChanged();
    void messageChanged();
    void busyChanged();
    void silencesChanged();
    /// A new immutable document snapshot is available.
    void snapshotChanged();

private:
    using Mutation = std::function<Status(project::Project&)>;
    /// Applies `edit` to a copy of the document; on success it becomes the
    /// document and one undo step (merged with the previous step when both
    /// carry the same `mergeKey` within a short time, e.g. slider drags).
    bool mutate(const QString& label, const Mutation& edit, const QString& mergeKey = {});
    /// The edit that makes a layer visible at a rect (ProjectCanvas.cpp); empty when it cannot apply.
    [[nodiscard]] Mutation layerRectEdit(const QString& clipId, double seconds, double x, double y, double w, double h,
                                         QString& label) const;
    void documentChanged();
    void rebuildViews();
    void rebuildSelection();
    /// The selected layer becomes the selected clip's layer (when a clip is selected).
    void followClipTrack(bool notify = true);
    void scheduleSave();
    void showMessage(const QString& text);
    /// A message that reports success (showMessage reports problems; an assistant edit then fails).
    void showInfo(const QString& text);
    void setBusy(const QString& text);
    [[nodiscard]] timeline::edit::MediaBounds mediaBounds(const project::Project& p) const;
    [[nodiscard]] static std::optional<timeline::ClipId> clipIdFrom(const QString& id);
    [[nodiscard]] static std::optional<timeline::TrackId> trackIdFrom(const QString& id);

    struct UndoStep {
        project::Project before;
        QString label;
        QString mergeKey;
        qint64 atMs = 0;
        QString assistantId;
    };

    std::unique_ptr<SerialExecutor> worker_;
    std::shared_ptr<project::Project> project_;
    std::shared_ptr<const project::Project> snapshot_;
    std::filesystem::path dir_;
    bool loading_ = false;
    std::vector<UndoStep> undo_;
    std::vector<UndoStep> redo_;
    bool dirty_ = false;
    QTimer saveTimer_;
    QString notice_;
    QString message_;
    QString busyText_;
    QString selectedClip_;
    QStringList selectedClips_;
    QString selectedTrack_;
    QString linkedEditMode_ = QStringLiteral("track");
    QVariantMap selection_;
    QVariantList tracks_;
    QVariantList markers_;
    QVariantList layoutRegions_;
    QVariantList layoutPresets_;
    QString layoutPreset_;
    QVariantList subtitles_;
    QVariantMap style_;
    QVariantMap roleTransforms_;
    QVariantList textOverlays_;
    QString screenMedia_;
    QString cameraMedia_;
    std::vector<TimeRange> silences_;
    std::shared_ptr<std::atomic<bool>> jobCancel_;
    bool assistantEditing_ = false;
    QString assistantError_;
};

}  // namespace lectern::ui
