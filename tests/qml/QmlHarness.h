#pragma once

// Loads a QML snippet that imports Lectern.UI into an offscreen window and
// sends it real mouse/keyboard events (QTest), so tests exercise the QML the
// app ships, not a copy of its logic.

#include "ui/IconProvider.h"

#include <QCoreApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>

#include <gtest/gtest.h>

#include <chrono>
#include <functional>
#include <memory>
#include <thread>

namespace lectern::test {

inline bool waitFor(const std::function<bool()>& pred, int timeoutMs = 10'000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        if (pred()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return pred();
}

/// Lets queued work (bindings, timers, deferred deletes) run for `ms`.
inline void settle(int ms = 60) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < until) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

class QmlHarness {
public:
    QmlHarness(const QByteArray& qml, const QVariantMap& properties, QSize size = {1100, 700}) {
        engine_.addImageProvider(QStringLiteral("icon"), new ui::IconProvider);
        QQmlComponent component(&engine_);
        component.setData(qml, QUrl(QStringLiteral("qrc:/lectern-test/Harness.qml")));
        if (component.isError()) {
            ADD_FAILURE() << component.errorString().toStdString();
            return;
        }
        QObject* object = component.createWithInitialProperties(properties);
        if (!object) {
            ADD_FAILURE() << component.errorString().toStdString();
            return;
        }
        root_.reset(qobject_cast<QQuickItem*>(object));
        if (!root_) {
            ADD_FAILURE() << "the harness root is not an Item";
            delete object;
            return;
        }
        window_.resize(size);
        root_->setParentItem(window_.contentItem());
        root_->setSize(QSizeF(size));
        window_.show();
        settle(100);
    }
    ~QmlHarness() { root_.reset(); }

    [[nodiscard]] bool ok() const { return root_ != nullptr; }
    [[nodiscard]] QQuickWindow& window() { return window_; }
    [[nodiscard]] QQuickItem* root() const { return root_.get(); }
    /// An item by objectName, also among delegates (Repeater items are only
    /// children in the visual tree, not QObject children).
    [[nodiscard]] QQuickItem* find(const char* objectName) const {
        if (!root_) return nullptr;
        const QString name = QString::fromLatin1(objectName);
        if (auto* item = root_->findChild<QQuickItem*>(name)) return item;
        return findInTree(root_.get(), name);
    }
    [[nodiscard]] static QQuickItem* findInTree(QQuickItem* item, const QString& name) {
        for (QQuickItem* child : item->childItems()) {
            if (child->objectName() == name) return child;
            if (auto* found = child->findChild<QQuickItem*>(name)) return found;
            if (auto* found = findInTree(child, name)) return found;
        }
        return nullptr;
    }

    /// A point of `item` (item coordinates) in window coordinates.
    [[nodiscard]] static QPoint at(const QQuickItem* item, QPointF local) {
        return item->mapToScene(local).toPoint();
    }

    void press(QPoint p, Qt::KeyboardModifiers m = {}) { QTest::mousePress(&window_, Qt::LeftButton, m, p); }
    /// QTest::mouseMove carries no keyboard modifiers; this does.
    void move(QPoint p, Qt::KeyboardModifiers m = {}) {
        QTest::mouseEvent(QTest::MouseMove, &window_, Qt::NoButton, m, p);
    }
    void release(QPoint p, Qt::KeyboardModifiers m = {}) { QTest::mouseRelease(&window_, Qt::LeftButton, m, p); }
    void click(QPoint p, Qt::KeyboardModifiers m = {}) {
        press(p, m);
        release(p, m);
        settle(30);
    }
    /// Types text into the focused item, one key at a time.
    void type(const QString& text) {
        for (const QChar c : text) QTest::keyClick(&window_, c.toLatin1());
        settle(20);
    }
    /// Press, move in steps (like a hand), release.
    void drag(QPoint from, QPoint to, Qt::KeyboardModifiers m = {}, int steps = 12) {
        press(from, m);
        for (int i = 1; i <= steps; ++i) {
            const QPointF p = QPointF(from) + (QPointF(to - from) * i / steps);
            move(p.toPoint(), m);
            settle(8);
        }
        release(to, m);
        settle(60);
    }

private:
    QQmlEngine engine_;
    QQuickWindow window_;
    std::unique_ptr<QQuickItem> root_;
};

}  // namespace lectern::test
