#include "obs-adapter.hpp"
#include <QFileInfo>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QProcess>
#include <QThread>
#include <QTimer>
#include <obs-hotkey.h>
#include <obs.h>
namespace bs {
ObsAdapter::ObsAdapter(QObject *p) : QObject(p) {
  obs_frontend_add_event_callback(callback, this);
}
ObsAdapter::~ObsAdapter() {
  obs_frontend_remove_event_callback(callback, this);
}
void ObsAdapter::callback(obs_frontend_event e, void *d) {
  auto *self = static_cast<ObsAdapter *>(d);
  if (QThread::currentThread() == self->thread())
    self->event(e);
  else
    QMetaObject::invokeMethod(
        self, [self, e] { self->event(e); }, Qt::QueuedConnection);
}
void ObsAdapter::event(obs_frontend_event e) {
  bool rec = e == OBS_FRONTEND_EVENT_RECORDING_STARTED ||
             e == OBS_FRONTEND_EVENT_RECORDING_STOPPED;
  bool stream = e == OBS_FRONTEND_EVENT_STREAMING_STARTED ||
                e == OBS_FRONTEND_EVENT_STREAMING_STOPPED;
  if (!rec && !stream)
    return;
  auto &o = rec ? recording : streaming;
  bool started = e == OBS_FRONTEND_EVENT_RECORDING_STARTED ||
                 e == OBS_FRONTEND_EVENT_STREAMING_STARTED;
  if (started) {
    if (!o.pending.isEmpty()) {
      o.owners.insert(o.pending);
      o.owners.unite(o.waiting);
      o.waiting.clear();
      o.pending.clear();
      emit completed(o.pendingKey, "succeeded", "OBS confirmed output started");
      o.pendingKey.clear();
    }
    emit diagnostic("info",
                    rec ? "OBS recording started" : "OBS streaming started");
  } else {
    QString restart = o.restart;
    QString restartKey = o.pendingKey;
    if (!o.stopKey.isEmpty())
      emit completed(o.stopKey, "succeeded", "OBS confirmed output stopped");
    else if (!o.pendingKey.isEmpty() && restart.isEmpty())
      emit completed(o.pendingKey, "failed",
                     "OBS output stopped before start confirmation");
    o = {};
    emit diagnostic("info",
                    rec ? "OBS recording stopped" : "OBS streaming stopped");
    if (!restart.isEmpty()) {
      o.pending = restart;
      o.pendingKey = restartKey;
      o.requested = now();
      if (rec)
        obs_frontend_recording_start();
      else
        obs_frontend_streaming_start();
    }
  }
}
QJsonObject ObsAdapter::status() const {
  return {{"recording", obs_frontend_recording_active()},
          {"streaming", obs_frontend_streaming_active()},
          {"recording_owned", !recording.owners.isEmpty()},
          {"streaming_owned", !streaming.owners.isEmpty()}};
}
Outcome ObsAdapter::output(const Due &d, const QJsonObject &s) {
  bool rec = d.action.type.startsWith("record");
  auto &o = rec ? recording : streaming;
  bool active =
      rec ? obs_frontend_recording_active() : obs_frontend_streaming_active();
  bool owned = !o.owners.isEmpty();
  emit diagnostic(
      "debug", (rec ? "record" : "stream") +
                    QString(" action %1 active=%2 pending=%3 owners=%4")
                        .arg(d.action.type)
                        .arg(active)
                        .arg(!o.pending.isEmpty())
                        .arg(o.owners.size()));
  bool allow = s["allow_unowned_stop"].toBool(false);
  auto type = d.action.type;
  if (!active && !o.pending.isEmpty() && now() - o.requested > 30000) {
    o.pending.clear();
    o.owners.clear();
  }
  if (type.endsWith(".start")) {
    if (!o.pending.isEmpty()) {
      o.waiting.insert(d.event.id);
      return {"skipped", "Joined pending scheduled output"};
    }
    if (active) {
      auto policy =
          s[rec ? "record_existing" : "stream_existing"].toString("leave");
      if (policy == "restart") {
        if (!stopAllowed(active, owned, allow))
          return {"skipped", "Existing output belongs to the operator"};
        if (o.owners.size() > 1 ||
            (!o.owners.isEmpty() && !o.owners.contains(d.event.id)))
          return {"skipped", "Other events still own this output"};
        o.restart = d.event.id;
        o.pendingKey = d.key();
        if (rec)
          obs_frontend_recording_stop();
        else
          obs_frontend_streaming_stop();
        return {"requested", "Output restart requested"};
      }
      if (owned && policy != "ignore")
        o.owners.insert(d.event.id);
      return {"skipped", "Output already active; left running"};
    }
    o.pending = d.event.id;
    o.pendingKey = d.key();
    o.requested = now();
    emit diagnostic("debug", rec ? "calling obs_frontend_recording_start"
                                  : "calling obs_frontend_streaming_start");
    if (rec)
      obs_frontend_recording_start();
    else
      obs_frontend_streaming_start();
    // OBS 32 currently routes the frontend wrapper through its main QObject.
    // Keep the wrapper as the first path; this queued fallback handles builds
    // where the wrapper is called from a frontend plugin-owned QObject thread.
    QTimer::singleShot(100, this, [this, rec, key = d.key()] {
      auto &output = rec ? recording : streaming;
      bool running = rec ? obs_frontend_recording_active()
                         : obs_frontend_streaming_active();
      if (output.pendingKey != key || running)
        return;
      auto *main = static_cast<QObject *>(obs_frontend_get_main_window());
      const char *method = rec ? "StartRecording" : "StartStreaming";
      bool invoked = main && QMetaObject::invokeMethod(main, method,
                                                        Qt::QueuedConnection);
      emit diagnostic("debug", QString("frontend fallback %1: %2")
                                  .arg(method, invoked ? "queued" : "not available"));
    });
    QTimer::singleShot(30000, this, [this, rec, key = d.key()] {
      auto &o = rec ? recording : streaming;
      if (!o.pending.isEmpty() && o.pendingKey == key) {
        emit completed(key, "failed",
                       "OBS did not confirm start within 30 seconds");
        o.pending.clear();
        o.pendingKey.clear();
        o.waiting.clear();
        emit diagnostic(
            "error",
            rec ? "OBS recording did not confirm start within 30 seconds"
                : "OBS streaming did not confirm start within 30 seconds");
      }
    });
    return {"requested",
            "Native start requested; OBS callbacks log confirmation"};
  }
  if (type.endsWith(".stop")) {
    if (!stopAllowed(active, owned, allow))
      return {"skipped",
              active ? "Output belongs to the operator" : "Output inactive"};
    if (owned && d.event.id != "operator") {
      if (!o.owners.contains(d.event.id))
        return {"skipped", "This event does not own the output"};
      if (o.owners.size() > 1) {
        o.owners.remove(d.event.id);
        return {"skipped", "Other scheduled events still require this output"};
      }
    }
    o.stopKey = d.key();
    emit diagnostic("debug", rec ? "calling obs_frontend_recording_stop"
                                  : "calling obs_frontend_streaming_stop");
    if (rec)
      obs_frontend_recording_stop();
    else
      obs_frontend_streaming_stop();
    return {"requested",
            "Native stop requested; OBS callbacks log confirmation"};
  }
  if (!rec || !active || (!owned && !allow))
    return {"skipped",
            "Cannot pause/resume an inactive or operator-owned output"};
  obs_frontend_recording_pause(type == "record.pause");
  return {"requested", "Recording pause state requested"};
}
Outcome ObsAdapter::execute(const Due &d, const QJsonObject &s) {
  Q_ASSERT(QThread::currentThread() == thread());
  auto t = d.action.type;
  auto p = d.action.parameters;
  if (t.startsWith("record.") || t.startsWith("stream."))
    return output(d, s);
  if (t.startsWith("scene.")) {
    auto name = p["scene"].toString();
    auto *source = obs_get_source_by_name(name.toUtf8().constData());
    if (!source)
      return {"failed", "Scene not found"};
    bool scene = obs_scene_from_source(source) != nullptr;
    bool preview = t == "scene.preview";
    if (scene && (!preview || obs_frontend_preview_program_mode_active())) {
      if (preview)
        obs_frontend_set_current_preview_scene(source);
      else
        obs_frontend_set_current_scene(source);
    } else {
      obs_source_release(source);
      return {"failed", "Invalid scene or studio mode is disabled"};
    }
    obs_source_release(source);
    return {"succeeded", "Scene selected"};
  }
  if (t == "source.show" || t == "source.hide") {
    auto *source =
        obs_get_source_by_name(p["scene"].toString().toUtf8().constData());
    if (!source)
      return {"failed", "Scene not found"};
    auto *scene = obs_scene_from_source(source);
    auto *item = scene ? obs_scene_find_source_recursive(
                             scene, p["source"].toString().toUtf8().constData())
                       : nullptr;
    if (item)
      obs_sceneitem_set_visible(item, t == "source.show");
    obs_source_release(source);
    return {item ? "succeeded" : "failed",
            item ? "Visibility changed" : "Scene item not found"};
  }
  if (t == "source.enable" || t == "source.disable") {
    auto *source =
        obs_get_source_by_name(p["source"].toString().toUtf8().constData());
    if (!source)
      return {"failed", "Source not found"};
    obs_source_set_enabled(source, t == "source.enable");
    obs_source_release(source);
    return {"succeeded", "Source enabled state changed"};
  }
  if (t == "replay.start") {
    if (!obs_frontend_replay_buffer_active())
      obs_frontend_replay_buffer_start();
    return {"requested", "Replay buffer start requested"};
  }
  if (t == "replay.stop") {
    obs_frontend_replay_buffer_stop();
    return {"requested", "Replay buffer stop requested"};
  }
  if (t == "replay.save") {
    if (!obs_frontend_replay_buffer_active())
      return {"failed", "Replay buffer is inactive"};
    obs_frontend_replay_buffer_save();
    return {"requested", "Replay save requested"};
  }
  if (!s["advanced_actions"].toBool())
    return {"skipped", "Advanced actions are disabled in Settings"};
  if (t == "hotkey.trigger") {
    // Hotkeys may invoke arbitrary third-party callbacks: refuse while an
    // unowned output runs.
    if ((obs_frontend_recording_active() && recording.owners.isEmpty()) ||
        (obs_frontend_streaming_active() && streaming.owners.isEmpty()))
      return {"skipped",
              "Hotkeys disabled while operator-owned output is active"};
    struct Search {
      QByteArray name;
      obs_hotkey_id id = OBS_INVALID_HOTKEY_ID;
    } search{p["name"].toString().toUtf8()};
    obs_enum_hotkeys(
        [](void *data, obs_hotkey_id id, obs_hotkey_t *h) {
          auto *s = static_cast<Search *>(data);
          if (s->name == obs_hotkey_get_name(h)) {
            s->id = id;
            return false;
          }
          return true;
        },
        &search);
    if (search.id == OBS_INVALID_HOTKEY_ID)
      return {"failed", "Hotkey not found"};
    obs_hotkey_trigger_routed_callback(search.id, true);
    obs_hotkey_trigger_routed_callback(search.id, false);
    return {"requested", "Hotkey triggered"};
  }
  if (t == "command.run") {
    auto program = p["program"].toString();
    if (!QFileInfo(program).isAbsolute() || !QFileInfo(program).isExecutable())
      return {"failed", "Command requires an absolute executable path"};
    auto base = QFileInfo(program).baseName().toLower();
    if (QStringList{"sh", "bash", "zsh", "cmd", "powershell", "pwsh"}.contains(
            base))
      return {"failed", "Shell interpreters are prohibited"};
    auto *process = new QProcess(this);
    QStringList args;
    for (auto v : p["arguments"].toArray())
      args.append(v.toString());
    process->setStandardOutputFile(QProcess::nullDevice());
    process->setStandardErrorFile(QProcess::nullDevice());
    connect(process, &QProcess::finished, this,
            [this, process, key=d.key()](int code, QProcess::ExitStatus status) {
              emit completed(key, code == 0 && status == QProcess::NormalExit ? "succeeded" : "failed", "Local command completed with exit code " + QString::number(code));
              emit diagnostic(code == 0 && status == QProcess::NormalExit
                                  ? "info"
                                  : "error",
                              "Local command completed with exit code " +
                                  QString::number(code));
              process->deleteLater();
            });
    connect(process, &QProcess::errorOccurred, this,
            [this, process, key=d.key()](QProcess::ProcessError) {
              emit completed(key, "failed", "Local command could not execute");
              emit diagnostic("error", "Local command could not execute");
              process->deleteLater();
            });
    QTimer::singleShot(qBound(1, p["timeout_seconds"].toInt(60), 3600) * 1000,
                       process, [process] {
                         if (process->state() != QProcess::NotRunning)
                           process->kill();
                       });
    process->start(program, args);
    return {"requested", "Local command launched without a shell"};
  }
  if (t == "webhook.http") {
    QUrl url(p["url"].toString());
    if (!url.isValid() || !url.userInfo().isEmpty() ||
        (url.scheme() != "https" &&
         !(url.scheme() == "http" &&
           (url.host() == "127.0.0.1" || url.host() == "localhost"))))
      return {"failed",
              "Webhook requires HTTPS or loopback HTTP, without userinfo"};
    QNetworkRequest req(url);
    req.setTransferTimeout(15000);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::ManualRedirectPolicy);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    auto *r = network.post(
        req,
        QJsonDocument(p["body"].toObject()).toJson(QJsonDocument::Compact));
    connect(r, &QNetworkReply::finished, this, [this, r, key=d.key()] {
      auto code =
          r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
      emit completed(key, r->error()==QNetworkReply::NoError && code>=200 && code<300 ? "succeeded" : "failed", "Webhook completed with HTTP status " + QString::number(code));
      emit diagnostic(
          r->error() == QNetworkReply::NoError && code >= 200 && code < 300
              ? "info"
              : "error",
          "Webhook completed with HTTP status " + QString::number(code));
      r->deleteLater();
    });
    return {"requested", "Webhook dispatched"};
  }
  return {"failed", "Unknown action"};
}
} // namespace bs
