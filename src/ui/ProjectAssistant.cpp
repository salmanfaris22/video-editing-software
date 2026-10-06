#include "ui/ProjectController.h"

#include "core/Log.h"

#include <QDateTime>

namespace lectern::ui {

QString ProjectController::assistantEdit(const QString& clientId, const QString& label,
                                          const std::function<void()>& edit) {
    if (!project_ || loading_ || busy()) return QStringLiteral("Open an idle project before editing.");
    if (assistantEditing_) return QStringLiteral("An assistant edit is already running.");
    auto before = *project_;
    const auto selectionBefore = selectedClips_;
    const auto trackBefore = selectedTrack_;
    const auto modeBefore = linkedEditMode_;
    assistantError_.clear();
    message_.clear();
    assistantEditing_ = true;
    try { edit(); }
    catch (const std::exception& ex) { assistantError_ = QString::fromUtf8(ex.what()); }
    catch (...) { assistantError_ = QStringLiteral("The assistant edit failed."); }
    assistantEditing_ = false;
    setLinkedEditMode(modeBefore);
    if (!assistantError_.isEmpty()) {
        LEC_WARN("mcp", "assistant edit by {} failed ({}): {}", clientId.toStdString(), label.toStdString(), assistantError_.toStdString());
        *project_ = std::move(before);
        selectedClips_ = selectionBefore;
        selectedClip_ = selectedClips_.isEmpty() ? QString() : selectedClips_.last();
        selectedTrack_ = trackBefore;
        documentChanged();
        return assistantError_;
    }
    if (*project_ == before) return {};
    LEC_INFO("mcp", "assistant edit by {}: {}", clientId.toStdString(), label.toStdString());
    undo_.push_back({std::move(before), QStringLiteral("Assistant: ") + label, {},
                     QDateTime::currentMSecsSinceEpoch(), clientId});
    if (undo_.size() > 200) undo_.erase(undo_.begin());
    redo_.clear();
    documentChanged();
    scheduleSave();
    return {};
}

int ProjectController::undoAssistantEdits(const QString& clientId) {
    if (clientId.isEmpty()) return 0;
    int count = 0;
    // Never undo a person's intervening edit or another assistant's work.
    while (!undo_.empty() && undo_.back().assistantId == clientId) { undo(); ++count; }
    return count;
}

} // namespace lectern::ui
