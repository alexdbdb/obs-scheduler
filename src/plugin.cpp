#include "calendar.hpp"
#include "obs-adapter.hpp"
#include "ui.hpp"
#include <QEventLoop>
#include <QFileInfo>
#include <QMainWindow>
#include <QPointer>
#include <QThread>
#include <obs-module.h>
#include <util/platform.h>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("broadcast-scheduler", "en-US")
MODULE_EXPORT const char *obs_module_description(void) {
  return "Broadcast Scheduler: native calendar automation for OBS";
}
MODULE_EXPORT const char *obs_module_name(void) {
  return "Broadcast Scheduler";
}
MODULE_EXPORT const char *obs_module_author(void) {
  return "AUTHOR_NAME (replace before publishing)";
}
namespace {
QThread *worker = nullptr;
bs::Runtime *runtime = nullptr;
bs::ObsAdapter *adapter = nullptr;
QPointer<bs::Dock> dock;
bool frontendRegistered = false;
bool dockRegistered = false;
void shutdown() {
  if (!worker)
    return;
  if (dockRegistered) {
    obs_frontend_remove_dock("broadcast-scheduler");
    dockRegistered = false;
    dock = nullptr;
  }
  if (!worker->isRunning()) {
    delete runtime; runtime = nullptr;
    delete worker; worker = nullptr;
    delete adapter; adapter = nullptr;
    return;
  }
  // Worker may be waiting for an OBS call. Keep the UI event loop alive while
  // it drains.
  auto *w = worker;
  QEventLoop loop;
  QObject::connect(w, &QThread::finished, &loop, &QEventLoop::quit);
  QMetaObject::invokeMethod(
      runtime,
      [] {
        runtime->shutdown();
        QThread::currentThread()->quit();
      },
      Qt::QueuedConnection);
  loop.exec();
  w->wait();
  delete w;
  worker = nullptr;
  runtime = nullptr;
  delete adapter;
  adapter = nullptr;
}
void frontend(obs_frontend_event e, void *) {
  if (e == OBS_FRONTEND_EVENT_FINISHED_LOADING && worker &&
      !worker->isRunning()) {
    auto *main = static_cast<QObject *>(obs_frontend_get_main_window());
    if (main)
      QMetaObject::invokeMethod(
          main, [] {
            if (worker && !worker->isRunning())
              worker->start();
          },
          Qt::QueuedConnection);
  }
  if (e == OBS_FRONTEND_EVENT_EXIT)
    shutdown();
}
} // namespace
bool obs_module_load(void) {
  blog(LOG_INFO, "[broadcast-scheduler] version %s loaded", PLUGIN_VERSION);
  return true;
}
void obs_module_post_load(void) {
  blog(LOG_INFO, "[broadcast-scheduler] post-load initialization started");
  try {
    char *zones = obs_module_file("zoneinfo");
    if (zones) {
      if (QFileInfo(QString::fromUtf8(zones)).isDir())
        bs::CalendarParser::setZoneDirectory(QString::fromUtf8(zones));
      bfree(zones);
    }
    auto *parent = static_cast<QMainWindow *>(obs_frontend_get_main_window());
    char *config = obs_module_config_path("scheduler.sqlite3");
    QString path = QString::fromUtf8(config);
    bfree(config);
    adapter = new bs::ObsAdapter(parent);
    worker = new QThread;
    runtime = new bs::Runtime(path);
    runtime->moveToThread(worker);
    runtime->action = [](const bs::Due &d, const QJsonObject &s) {
      bs::Outcome result;
      auto *main = static_cast<QObject *>(obs_frontend_get_main_window());
      QMetaObject::invokeMethod(
          main,
          [&] { result = adapter->execute(d, s); },
          Qt::BlockingQueuedConnection);
      return result;
    };
    runtime->obsStatus = [] {
      QJsonObject status;
      auto *main = static_cast<QObject *>(obs_frontend_get_main_window());
      QMetaObject::invokeMethod(
          main,
          [&] { status = adapter->status(); },
          Qt::BlockingQueuedConnection);
      return status;
    };
    QObject::connect(adapter, &bs::ObsAdapter::diagnostic, runtime,
                     [](QString level, QString message) {
                       blog(level == "error" ? LOG_WARNING
                                              : level == "debug" ? LOG_DEBUG : LOG_INFO,
                            "[broadcast-scheduler] %s",
                            message.toUtf8().constData());
                       runtime->command("diagnostic", {{"level", level},
                                                       {"message", message}});
                     });
    QObject::connect(adapter, &bs::ObsAdapter::completed, runtime,
                     [](QString key, QString result, QString message) {
                       runtime->command("action.complete", {{"key", key},
                         {"result", result}, {"message", message}});
                     });
    dock = new bs::Dock(runtime, parent);
    if (!obs_frontend_add_dock_by_id("broadcast-scheduler",
                                     "Broadcast Scheduler", dock)) {
      blog(LOG_ERROR, "[broadcast-scheduler] obs_frontend_add_dock_by_id returned false");
      delete dock;
      delete runtime;
      runtime = nullptr;
      delete worker;
      worker = nullptr;
      delete adapter;
      adapter = nullptr;
      blog(LOG_ERROR, "[broadcast-scheduler] Dock registration failed");
      return;
    }
    if (dock)
      dock->window()->show();
    dockRegistered = true;
    blog(LOG_INFO, "[broadcast-scheduler] dock registered; starting runtime");
    QObject::connect(worker, &QThread::started, runtime, &bs::Runtime::start);
    QObject::connect(worker, &QThread::finished, runtime,
                     &QObject::deleteLater);
    obs_frontend_add_event_callback(frontend, nullptr);
    frontendRegistered = true;
    // obs_module_post_load may run after FINISHED_LOADING has already been
    // emitted. Start here as well; QThread::start is idempotent while the
    // thread is running, and the frontend callback remains useful for hosts
    // that load the module earlier in their startup sequence.
    if (!worker->isRunning())
      QMetaObject::invokeMethod(
          parent, [] {
            if (worker && !worker->isRunning())
              worker->start();
          },
          Qt::QueuedConnection);
  } catch (const std::exception &e) {
    blog(LOG_ERROR, "[broadcast-scheduler] Initialization failed: %s",
         e.what());
  }
}
void obs_module_unload(void) {
  if (frontendRegistered) {
    obs_frontend_remove_event_callback(frontend, nullptr);
    frontendRegistered = false;
  }
  shutdown();
}
