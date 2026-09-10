#include "obs-adapter.hpp"
#include <QThread>
#include <QTimer>
#include <util/config-file.h>
namespace bs {
ObsAdapter::ObsAdapter(QObject *parent) : QObject(parent) {
  obs_frontend_add_event_callback(callback, this);
}
ObsAdapter::~ObsAdapter() { obs_frontend_remove_event_callback(callback, this); }
void ObsAdapter::callback(obs_frontend_event e, void *data) {
  auto *self = static_cast<ObsAdapter *>(data);
  if (QThread::currentThread() == self->thread()) self->event(e);
  else QMetaObject::invokeMethod(self, [self, e] { self->event(e); }, Qt::QueuedConnection);
}
void ObsAdapter::stop() {
  if (stopping || !owned) return;
  stopping = true;
  obs_frontend_recording_stop();
  const auto key = stopKey;
  QTimer::singleShot(30000, this, [this, key] {
    if (stopping && stopKey == key) {
      if (!key.isEmpty()) emit completed(key, "indeterminate", "OBS has not confirmed recording stop");
      emit diagnostic("error", "OBS has not confirmed recording stop");
    }
  });
}
void ObsAdapter::event(obs_frontend_event e) {
  if (e == OBS_FRONTEND_EVENT_RECORDING_STARTED) {
    if (starting) {
      owned = true;
      starting = false;
      for (const auto &key : startKeys)
        emit completed(key, "succeeded", "OBS confirmed recording started");
      startKeys.clear();
      // Retain ownership after a delayed confirmation, including one arriving
      // after the scheduled end. Never leave that recording running indefinitely.
      for (auto i = owners.begin(); i != owners.end();) {
        if (i.value() <= now()) i = owners.erase(i);
        else ++i;
      }
      if (stopWhenStarted || owners.isEmpty()) stop();
    }
    emit diagnostic("info", "OBS recording started");
  } else if (e == OBS_FRONTEND_EVENT_RECORDING_STOPPED) {
    for (const auto &key : startKeys)
      emit completed(key, "failed", "OBS stopped before confirming recording start");
    if (!stopKey.isEmpty())
      emit completed(stopKey, "succeeded", "OBS confirmed recording stopped");
    owners.clear();
    startKeys.clear();
    stopKey.clear();
    starting = owned = stopping = stopWhenStarted = false;
    emit diagnostic("info", "OBS recording stopped");
  }
}
QJsonObject ObsAdapter::status() const {
  return {{"recording", obs_frontend_recording_active()}, {"recording_owned", owned}};
}
Outcome ObsAdapter::execute(const Due &d, const QJsonObject &) {
  Q_ASSERT(QThread::currentThread() == thread());
  const bool active = obs_frontend_recording_active();
  if (d.action.type == "record.start") {
    if (stopping)
      return {"failed", "Previous recording is still stopping"};
    if (active && !owned)
      return {"skipped", "Recording belongs to the operator; left unchanged"};
    owners[d.event.id] = d.event.end;
    if (active)
      return {"succeeded", "Joined scheduled recording"};
    startKeys[d.event.id] = d.key();
    if (starting) {
      stopWhenStarted = false;
      return {"requested", "Joined pending scheduled recording"};
    }
    starting = true;
    stopWhenStarted = false;
    auto *profile = obs_frontend_get_profile_config();
    QByteArray previousFormat;
    if (profile) {
      const auto *value =
          config_get_string(profile, "Output", "FilenameFormatting");
      if (value)
        previousFormat = value;
      const auto format = recordingFilename(d.event).toUtf8();
      config_set_string(profile, "Output", "FilenameFormatting",
                        format.constData());
    }
    obs_frontend_recording_start();
    if (profile)
      config_set_string(profile, "Output", "FilenameFormatting",
                        previousFormat.constData());
    QTimer::singleShot(30000, this, [this] {
      if (starting) {
        for (const auto &key : startKeys)
          emit completed(key, "indeterminate", "OBS has not confirmed recording start");
        emit diagnostic("error", "Recording start is unconfirmed; awaiting OBS");
      }
    });
    return {"requested", "Recording start requested"};
  }
  if (d.action.type == "record.stop") {
    if (!owners.contains(d.event.id))
      return {"skipped", active ? "This event does not own the recording" : "Recording inactive"};
    owners.remove(d.event.id);
    if (!owners.isEmpty())
      return {"succeeded", "Event finished; other events still need the recording"};
    stopKey = d.key();
    if (starting) {
      stopWhenStarted = true;
      return {"requested", "Recording will stop when OBS confirms its pending start"};
    }
    if (!active || !owned)
      return {"skipped", "Recording is inactive or belongs to the operator"};
    stop();
    return {"requested", "Recording stop requested"};
  }
  return {"failed", "Only scheduled recording start and stop are supported"};
}
} // namespace bs
