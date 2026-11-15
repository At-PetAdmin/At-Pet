#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QString>
#include <QVector>
#include <algorithm>

// One presentation lane per destination. Incoming state remains lossless in the
// controller; only its visual projection is coalesced, so tools never block.
class AgentEventScheduler {
public:
    struct Candidate { QString key, animation, message; };
    AgentEventScheduler() { clock_.start(); }
    static int priority(const QString &animation) {
        if (animation == "permission") return 3;
        if (animation == "error") return 2;
        if (animation == "done" || animation == "cancelled") return 1;
        return 0;
    }
    QString select(const QString &destination, QVector<Candidate> candidates, qint64 now = -1) {
        if (now < 0) now = clock_.elapsed();
        if (candidates.isEmpty()) { lanes_[destination].selected.clear(); return {}; }
        int highest = 0;
        for (const auto &item : candidates) highest = qMax(highest, priority(item.animation));
        candidates.erase(std::remove_if(candidates.begin(), candidates.end(), [highest](const Candidate &c) {
            return priority(c.animation) != highest;
        }), candidates.end());
        std::sort(candidates.begin(), candidates.end(), [](const Candidate &a, const Candidate &b) { return a.key < b.key; });
        auto &lane = lanes_[destination];
        const auto current = std::find_if(candidates.cbegin(), candidates.cend(), [&](const Candidate &c) { return c.key == lane.selected; });
        if (current != candidates.cend() && now - lane.selectedAt < 1800) return lane.selected;
        auto next = std::find_if(candidates.cbegin(), candidates.cend(), [&](const Candidate &c) { return c.key > lane.selected; });
        if (next == candidates.cend()) next = candidates.cbegin();
        lane.selected = next->key;
        lane.selectedAt = now;
        return lane.selected;
    }
    bool present(const QString &destination, const QString &key, const QString &animation,
                 const QString &message, qint64 now = -1) {
        if (now < 0) now = clock_.elapsed();
        auto &lane = lanes_[destination];
        if (lane.shownKey == key && lane.animation == animation && lane.message == message) return false;
        const bool urgent = priority(animation) > priority(lane.animation);
        if (lane.shownKey == key && !urgent && now - lane.presentedAt < 800) return false;
        lane.shownKey = key;
        lane.animation = animation;
        lane.message = message;
        lane.presentedAt = now;
        return true;
    }
    void forget(const QString &destination) { lanes_.remove(destination); }
private:
    struct Lane {
        QString selected, shownKey, animation, message;
        qint64 selectedAt = 0, presentedAt = 0;
    };
    QHash<QString, Lane> lanes_;
    QElapsedTimer clock_;
};
