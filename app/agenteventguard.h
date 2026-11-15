#pragma once

#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QQueue>
#include <QSet>
#include <cmath>

// Ingestion protection is independent of presentation and survives the UI's
// five-second terminal-state cleanup. Ordering is scoped to client/agent/session/run.
class AgentEventGuard {
public:
    // Local dialog decisions are a separate control channel, not source lifecycle
    // events. They must not consume or fabricate a sender's sequence number.
    bool permissionEvent(const QString &key, QJsonObject *event) {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        prune(now);
        if (!states_.contains(key) && states_.size() >= 4096) return false;
        auto found = states_.find(key);
        if (found != states_.end() && (found->closed || found->terminal)) return false;
        auto &state = states_[key];
        state.touched = now;
        if (event->value("event").toString() == "permission_denied") {
            state.terminal = true;
            state.tools.clear();
        }
        event->insert("activeToolCount", int(state.tools.size()));
        return true;
    }
    bool accept(const QString &key, QJsonObject *event, qint64 now = QDateTime::currentMSecsSinceEpoch()) {
        const auto token = [](QString name) {
            QString result;
            for (const auto c : name.toLower()) if (c.isLetterOrNumber()) result += c;
            return result;
        };
        const QString name = token(event->value("event").toString());
        const QString run = event->value("runId").toString();
        const QString id = event->value("eventId").toString();
        const QString tool = event->value("toolCallId").toString();
        for (const QString &field : {QStringLiteral("runId"), QStringLiteral("eventId"), QStringLiteral("toolCallId")}) {
            if (event->contains(field) && (!event->value(field).isString() || event->value(field).toString().size() > 256))
                return false;
        }
        const bool sequenced = event->contains("sequence");
        const double sequence = event->value("sequence").toDouble(-1);
        if (sequenced && (!event->value("sequence").isDouble() || !std::isfinite(sequence) ||
                          sequence < 0 || sequence > 9007199254740991.0 || std::floor(sequence) != sequence || run.isEmpty()))
            return false;
        qint64 occurred = 0;
        if (event->contains("occurredAt")) {
            const auto date = QDateTime::fromString(event->value("occurredAt").toString(), Qt::ISODateWithMs);
            if (!date.isValid() || date.toMSecsSinceEpoch() > now + 300000) return false;
            occurred = date.toMSecsSinceEpoch();
        }
        prune(now);
        if (!states_.contains(key) && states_.size() >= 4096) return false;
        State state = states_.value(key);
        if ((!id.isEmpty() && state.ids.contains(id)) || (!run.isEmpty() && state.retiredRuns.contains(run))) return false;
        const bool opening = event->value("turnStart").toBool() || name == "started" || name == "sessionstart" ||
            name == "sessionstarted" || name == "turnstart" || name == "turnstarted" || name == "userpromptsubmit";
        const bool closing = name == "sessionend" || name == "sessionended" || name == "disconnect" || name == "disconnected";
        const bool toolStart = name == "toolstart" || name == "pretooluse" || name == "toolcall";
        const bool toolEnd = name == "toolend" || name == "toolcomplete" || name == "toolcompleted" || name == "posttooluse";
        const bool failure = name == "error" || name == "failed" || name == "exception" || name == "apierror";
        const bool terminal = closing || name == "done" || name == "completed" || name == "completion" || name == "stop" ||
            name == "turncomplete" || name == "agentturncomplete" || name == "cancelled" || name == "canceled" ||
            name == "aborted" || name == "interrupt" || name == "interrupted" || name == "permissiondenied" ||
            name == "approvaldenied" || (failure && tool.isEmpty());
        const bool newRun = !run.isEmpty() && run != state.run;
        const bool independentTool = !tool.isEmpty() && (toolStart || toolEnd || failure) &&
            !state.terminal && !state.closed && !opening && !newRun;
        if (!state.run.isEmpty() && run.isEmpty()) return false;
        if (state.terminal && opening && !state.run.isEmpty() && !newRun) return false;
        if (state.touched && newRun && !opening) return false;
        if (occurred && state.occurred && occurred < state.occurred && !independentTool) return false;
        if (sequenced && !newRun && state.sequenced && sequence <= state.sequence && !independentTool) return false;
        // Once a sender has opted into source ordering, it cannot bypass it by
        // omitting a sequence or timestamp on a later request in the same run.
        if (!newRun && ((state.sequenced && !sequenced) || (state.occurred && !occurred))) return false;
        if ((state.terminal || state.closed) && !opening && !closing && name != "idle") return false;
        if (state.closed && !opening) return false;
        if (state.terminal && terminal && !closing && !opening) return false;
        if (opening) {
            // A tombstone cannot be reopened by an unversioned legacy retry.
            if (state.closed && !newRun && (!occurred || occurred <= state.occurred)) return false;
            if (newRun) {
                if (!state.run.isEmpty()) {
                    state.retiredRuns.insert(state.run);
                    state.retiredOrder.enqueue(state.run);
                    if (state.retiredOrder.size() > 64) state.retiredRuns.remove(state.retiredOrder.dequeue());
                }
                state.run = run;
                state.sequenced = false;
                state.sequence = -1;
            }
            state.terminal = state.closed = false;
            state.tools.clear();
            state.finishedTools.clear();
        } else if (!state.touched) {
            state.run = run;
        }
        if (!tool.isEmpty() && toolStart) {
            if (state.finishedTools.contains(tool) || state.tools.contains(tool) || state.tools.size() >= 256) return false;
            state.tools.insert(tool);
        }
        if (!tool.isEmpty() && (toolEnd || failure)) {
            if (state.finishedTools.contains(tool) || state.finishedTools.size() >= 256) return false;
            state.tools.remove(tool);
            state.finishedTools.insert(tool);
            if (failure) event->insert("toolFailure", true);
        }
        if (terminal) { state.tools.clear(); state.terminal = true; }
        if (closing) state.closed = true;
        state.touched = now;
        if (occurred) state.occurred = qMax(state.occurred, occurred);
        if (sequenced) { state.sequence = qMax(state.sequence, sequence); state.sequenced = true; }
        if (!id.isEmpty()) {
            state.ids.insert(id);
            state.idOrder.enqueue(id);
            if (state.idOrder.size() > 256) state.ids.remove(state.idOrder.dequeue());
        }
        event->insert("activeToolCount", int(state.tools.size()));
        // One completed tool must not make an independently running tool idle.
        if (toolEnd && !state.tools.isEmpty()) event->insert("event", "tool_running");
        states_.insert(key, state);
        return true;
    }
private:
    static constexpr qint64 retentionMs = 30 * 60 * 1000;
    struct State {
        QString run;
        double sequence = -1;
        qint64 touched = 0, occurred = 0;
        bool sequenced = false, terminal = false, closed = false;
        QSet<QString> ids, tools, finishedTools, retiredRuns;
        QQueue<QString> idOrder, retiredOrder;
    };
    QHash<QString, State> states_;
    void prune(qint64 now) {
        for (auto it = states_.begin(); it != states_.end();) {
            if (it->touched < now - retentionMs) it = states_.erase(it); else ++it;
        }
    }
};
