#include "ui.hpp"
#include "messages.hpp"
#include <QApplication>
#include <QCheckBox>
#include <QDateTimeEdit>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFile>
#include <QFormLayout>
#include <QHostAddress>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QShortcut>
#include <QScrollArea>
#include <QSpinBox>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <utility>
#include <obs-module.h>
namespace bs {
QString tr(const char *key) { return QString::fromUtf8(obs_module_text(key)); }
namespace {
QString diagnostic(const QString &text) {
  static const MessageCatalog catalog([] {
    char *path = obs_module_file("messages.json");
    if (!path)
      return QJsonArray{};
    QFile file(QString::fromUtf8(path));
    bfree(path);
    if (!file.open(QIODevice::ReadOnly))
      return QJsonArray{};
    return QJsonDocument::fromJson(file.readAll()).array();
  }());
  return catalog.translate(text, [](const QString &key) {
    return bs::tr(key.toUtf8().constData());
  });
}
template <typename Function>
void runUiHandler(QWidget *parent, const char *context, Function &&function) {
  try {
    function();
  } catch (const std::exception &e) {
    blog(LOG_ERROR, "[broadcast-scheduler] UI handler '%s' failed: %s", context,
         e.what());
    QMessageBox::critical(parent, bs::tr("Error"), diagnostic(QString::fromUtf8(e.what())));
  } catch (...) {
    blog(LOG_ERROR, "[broadcast-scheduler] UI handler '%s' failed with an unknown exception",
         context);
    QMessageBox::critical(parent, bs::tr("Error"), bs::tr("InternalError"));
  }
}
QPushButton *button(QBoxLayout *l, const char *key, std::function<void()> fn) {
  auto *b = new QPushButton(bs::tr(key));
  l->addWidget(b);
  QObject::connect(b, &QPushButton::clicked, b, [b, key, fn = std::move(fn)] {
    runUiHandler(b, key, fn);
  });
  return b;
}
QTableWidget *table(QStringList headers) {
  auto *t = new QTableWidget(0, headers.size());
  t->setHorizontalHeaderLabels(headers);
  t->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
  t->horizontalHeader()->setStretchLastSection(true);
  t->setSelectionBehavior(QAbstractItemView::SelectRows);
  t->setSelectionMode(QAbstractItemView::SingleSelection);
  t->setEditTriggers(QAbstractItemView::NoEditTriggers);
  return t;
}
void row(QTableWidget *t, QStringList values, QString id = {}) {
  int r = t->rowCount();
  t->insertRow(r);
  int c = 0;
  for (auto &v : values) {
    auto *i = new QTableWidgetItem(v);
    i->setData(Qt::UserRole, id);
    t->setItem(r, c++, i);
  }
}
QComboBox *combo(QStringList choices, QString selected) {
  auto *c = new QComboBox;
  for (auto &s : choices) {
    const auto key = s == "ics" ? QString("ProviderICS")
                   : s == "file" ? QString("ProviderFile")
                   : s == "google" ? QString("ProviderGoogle")
                   : s == "odoo" ? QString("ProviderOdoo") : s;
    c->addItem(bs::tr(key.toUtf8().constData()), s);
  }
  int i = c->findData(selected);
  if (i >= 0)
    c->setCurrentIndex(i);
  return c;
}
QDialogButtonBox *buttons(QDialog *d, QVBoxLayout *l) {
  auto *b =
      new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
  l->addWidget(b);
  QObject::connect(b, &QDialogButtonBox::accepted, d, &QDialog::accept);
  QObject::connect(b, &QDialogButtonBox::rejected, d, &QDialog::reject);
  return b;
}
QSpinBox *spin(int value, int min, int max) {
  auto *s = new QSpinBox;
  s->setRange(min, max);
  s->setValue(value);
  return s;
}
QCheckBox *check(bool value) {
  auto *c = new QCheckBox;
  c->setChecked(value);
  return c;
}
QString display(const QString &s, const QString &zone) {
  return QDateTime::fromString(s, Qt::ISODateWithMs)
      .toTimeZone(QTimeZone(zone.toUtf8()))
      .toString("yyyy-MM-dd HH:mm:ss t");
}
} // namespace
void Dock::send(QString op, QJsonObject data) {
  QMetaObject::invokeMethod(
      runtime, [r = runtime, op, data] { r->command(op, data); },
      Qt::QueuedConnection);
}
Dock::Dock(Runtime *r, QWidget *p) : QWidget(p), runtime(r) {
  auto *l = new QVBoxLayout(this);
  dashboard = new QLabel;
  dashboard->setWordWrap(true);
  dashboard->setTextFormat(Qt::PlainText);
  l->addWidget(dashboard);
  auto *bar = new QHBoxLayout;
  l->addLayout(bar);
  button(bar, "ScheduleRecording", [this] { eventDialog(); });
  button(bar, "Calendars", [this] { calendars(); });
  button(bar, "Settings", [this] { settings(); });
  auto *bar2 = new QHBoxLayout;
  l->addLayout(bar2);
  button(bar2, "SyncNow", [this] { send("sync"); });
  button(bar2, "PauseScheduler", [this] {
    send(
        "settings.save",
        {{"enabled", !current["settings"].toObject()["enabled"].toBool(true)}});
  });
  button(bar2, "Recurrences", [this] { recurrences(); });
  auto *tabs = new QTabWidget;
  l->addWidget(tabs);
  auto *up = new QWidget;
  auto *ul = new QVBoxLayout(up);
  agenda = table({bs::tr("Title"), bs::tr("Start"), bs::tr("End"),
                  bs::tr("Source"), bs::tr("Enabled")});
  agenda->setSelectionMode(QAbstractItemView::ExtendedSelection);
  ul->addWidget(agenda);
  auto *actions = new QHBoxLayout;
  ul->addLayout(actions);
  auto selected = [this]() -> QJsonObject {
    int row = agenda->currentRow();
    if (row < 0)
      return {};
    auto id = agenda->item(row, 0)->data(Qt::UserRole).toString();
    for (auto v : current["events"].toArray())
      if (v.toObject()["id"].toString() == id)
        return v.toObject();
    return {};
  };
  button(actions, "Edit", [this, selected] {
    auto e = selected();
    if (!e.isEmpty())
      eventDialog(e);
  });
  button(actions, "Duplicate", [this, selected] {
    auto e = selected();
    if (!e.isEmpty())
      eventDialog(e, true);
  });
  auto deleteSelected = [this] {
    QList<QJsonObject> events;
    for (auto &index : agenda->selectionModel()->selectedRows(0)) {
      auto id = index.data(Qt::UserRole).toString();
      for (auto value : current["events"].toArray()) {
        auto event = value.toObject();
        if (event["id"].toString() == id) {
          events.append(event);
          break;
        }
      }
    }
    if (events.isEmpty())
      return;
    int imported = 0;
    for (auto &event : events) {
      const bool writable = event["source"].toString() == "Manual" ||
                            event["source"].toString() == "API";
      const bool external = !event["source_calendar"].toString().isEmpty() &&
                            !event["external_id"].toString().isEmpty();
      if (!writable && !external) {
        QMessageBox::information(this, bs::tr("Delete"),
                                 bs::tr("ReadOnlyEvent"));
        return;
      }
      if (external)
        ++imported;
    }
    QString confirmation;
    if (events.size() == 1)
      confirmation = imported ? bs::tr("ExcludeExternalEvent")
                              : bs::tr("DeleteEvent");
    else if (imported == events.size())
      confirmation = bs::tr("ExcludeExternalEvents");
    else if (imported == 0)
      confirmation = bs::tr("DeleteEvents");
    else
      confirmation = bs::tr("DeleteMixedEvents");
    if (QMessageBox::question(this, bs::tr("Delete"), confirmation) !=
        QMessageBox::Yes)
      return;
    for (auto &event : events)
      send("event.delete", event);
  };
  button(actions, "Delete", deleteSelected);
  auto *deleteShortcut = new QShortcut(QKeySequence::Delete, agenda);
  deleteShortcut->setContext(Qt::WidgetWithChildrenShortcut);
  connect(deleteShortcut, &QShortcut::activated, this,
          [this, deleteSelected] {
            runUiHandler(this, "Delete", deleteSelected);
          });
  tabs->addTab(up, bs::tr("Upcoming"));
  auto *cp = new QWidget;
  auto *cl = new QVBoxLayout(cp);
  view = combo({"Agenda", "Day", "Week", "Month"}, "Month");
  cl->addWidget(view);
  calendar = new QCalendarWidget;
  cl->addWidget(calendar);
  calendarEvents = table({bs::tr("Title"), bs::tr("Start"), bs::tr("End")});
  cl->addWidget(calendarEvents);
  connect(calendar, &QCalendarWidget::selectionChanged, this,
          &Dock::renderCalendar);
  connect(view, &QComboBox::currentIndexChanged, this, &Dock::renderCalendar);
  tabs->addTab(cp, bs::tr("Calendar"));
  history = table({bs::tr("Title"), bs::tr("Action"), bs::tr("Scheduled"), bs::tr("Actual"),
                   bs::tr("Result"), bs::tr("Message")});
  tabs->addTab(history, bs::tr("History"));
  logs = table({bs::tr("Actual"), bs::tr("Result"), bs::tr("Message")});
  tabs->addTab(logs, bs::tr("Logs"));
  connect(runtime, &Runtime::state, this, [this](QJsonObject data) {
    runUiHandler(this, "state", [this, data] { updateState(data); });
  });
  connect(runtime, &Runtime::problem, this,
          [this](QString m) { QMessageBox::warning(this, bs::tr("Error"), diagnostic(m)); });
  connect(runtime, &Runtime::openUrl, this,
          [](QString u) { QDesktopServices::openUrl(QUrl(u)); });
  connect(runtime, &Runtime::tokenGenerated, this, [this](QString t) {
    QDialog d(this);
    d.setWindowTitle(bs::tr("ApiToken"));
    auto *l = new QVBoxLayout(&d);
    auto *label = new QLabel(bs::tr("TokenOnce"));
    label->setWordWrap(true);
    l->addWidget(label);
    auto *line = new QLineEdit(t);
    line->setReadOnly(true);
    l->addWidget(line);
    buttons(&d, l);
    d.exec();
  });
  connect(runtime, &Runtime::googleCalendars, this, [this](QJsonArray list) {
    QDialog d(QApplication::activeModalWidget() ? QApplication::activeModalWidget() : this);
    d.setWindowTitle(bs::tr("GoogleCalendars"));
    auto *l = new QVBoxLayout(&d);
    auto *t =
        table({bs::tr("Enabled"), bs::tr("Title")});
    l->addWidget(t);
    auto existing = current["calendars"].toObject()["items"].toArray();
    for (auto v : list) {
      auto c = v.toObject();
      QJsonObject saved;
      for (auto x : existing)
        if (x.toObject()["kind"].toString() == "google" &&
            x.toObject()["location"] == c["id"])
          saved = x.toObject();
      row(t, {"", c["summary"].toString()},
          c["id"].toString());
      int r = t->rowCount() - 1;
      t->setCellWidget(r, 0, check(saved["enabled"].toBool(false)));
    }
    buttons(&d, l);
    d.resize(750, 400);
    if (d.exec() == QDialog::Accepted) {
      QJsonArray out;
      for (auto v : existing)
        if (v.toObject()["kind"].toString() != "google")
          out.append(v);
      for (int i = 0; i < t->rowCount(); ++i) {
        QString location = t->item(i, 0)->data(Qt::UserRole).toString();
        QJsonObject c;
        for (auto v : existing)
          if (v.toObject()["kind"].toString() == "google" &&
              v.toObject()["location"].toString() == location)
            c = v.toObject();
        if (c.isEmpty())
          c = {{"id", uid()},
               {"kind", "google"},
               {"location", location},
               {"name", t->item(i, 1)->text()},
               {"timezone", deviceZone()},
               {"refresh_minutes", 15}};
        c["enabled"] =
            static_cast<QCheckBox *>(t->cellWidget(i, 0))->isChecked();
        out.append(c);
      }
      send("calendars.save", {{"items", out}});
    }
  });
  connect(runtime, &Runtime::odooOptions, this, [this](QJsonObject options) {
    QDialog dialog(QApplication::activeModalWidget()
                       ? QApplication::activeModalWidget()
                       : this);
    dialog.setWindowTitle(bs::tr("OdooFilters"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *hint = new QLabel(bs::tr("OdooFiltersHelp"));
    hint->setWordWrap(true);
    layout->addWidget(hint);
    auto *filters = new QTabWidget;
    layout->addWidget(filters);
    const auto saved = current["odoo"].toObject();
    auto selectionPage = [&](const char *title, const QJsonArray &values,
                             const QJsonArray &selected) {
      auto *tableWidget = table({bs::tr("Enabled"), bs::tr("Title")});
      for (const auto value : values) {
        const auto item = value.toObject();
        row(tableWidget, {"", item["name"].toString()},
            QString::number(item["id"].toInt()));
        const bool checked = selected.isEmpty() || selected.contains(item["id"]);
        tableWidget->setCellWidget(tableWidget->rowCount() - 1, 0,
                                   check(checked));
      }
      filters->addTab(tableWidget, bs::tr(title));
      return tableWidget;
    };
    auto *types = selectionPage("OdooEventTypes", options["types"].toArray(),
                                saved["type_ids"].toArray());
    auto *stages = selectionPage("OdooStages", options["stages"].toArray(),
                                 saved["stage_ids"].toArray());
    auto *statesPage = new QWidget;
    auto *statesLayout = new QVBoxLayout(statesPage);
    const auto selectedStates = saved["kanban_states"].toArray();
    QList<QPair<QCheckBox *, QString>> states;
    for (const auto &state : {QString("normal"), QString("done"),
                              QString("blocked")}) {
      auto *box = new QCheckBox(bs::tr(("OdooState_" + state).toUtf8().constData()));
      box->setChecked(selectedStates.isEmpty() || selectedStates.contains(state));
      statesLayout->addWidget(box);
      states.append({box, state});
    }
    statesLayout->addStretch();
    filters->addTab(statesPage, bs::tr("OdooStates"));
    buttons(&dialog, layout);
    dialog.resize(620, 430);
    if (dialog.exec() == QDialog::Accepted) {
      auto selectedIds = [](QTableWidget *widget) {
        QJsonArray result;
        for (int rowIndex = 0; rowIndex < widget->rowCount(); ++rowIndex)
          if (static_cast<QCheckBox *>(widget->cellWidget(rowIndex, 0))->isChecked())
            result.append(widget->item(rowIndex, 0)->data(Qt::UserRole).toString().toInt());
        return result;
      };
      auto configuration = current["odoo"].toObject();
      configuration.remove("configured");
      configuration.remove("api_key_configured");
      configuration.remove("storage_error");
      configuration["type_ids"] = selectedIds(types);
      configuration["stage_ids"] = selectedIds(stages);
      QJsonArray selectedKanbanStates;
      for (const auto &state : states)
        if (state.first->isChecked())
          selectedKanbanStates.append(state.second);
      configuration["kanban_states"] = selectedKanbanStates;
      configuration["filters_initialized"] = true;
      send("odoo.configure", {{"configuration", configuration}});
      send("sync");
    }
  });
  auto *refresh = new QTimer(this);
  connect(refresh, &QTimer::timeout, this, [this] {
    QMetaObject::invokeMethod(runtime, &Runtime::snapshot,
                              Qt::QueuedConnection);
  });
  refresh->start(1000);
}
void Dock::updateState(QJsonObject data) {
  current = data;
  auto s = data["settings"].toObject();
  auto zone = deviceZone();
  auto obs = data["obs"].toObject();
  auto next = data["next"].toObject();
  QString text =
      bs::tr("Recording") + ": " +
      bs::tr(obs["recording"].toBool() ? "Active" : "Inactive") + "   " +
      "\n" +
      bs::tr("Scheduler") + ": " +
      bs::tr(!data["engine_error"].toString().isEmpty() ? "EngineStopped" : s["enabled"].toBool(true) ? "Running" : "Paused") +
      "   API: " + bs::tr(data["api_running"].toBool() ? "Running" : "Disabled") +
      "   " + zone;
  if (!next.isEmpty()) {
    auto seconds =
        now() / 1000 - instant(next["time"].toString()).toSecsSinceEpoch();
    text += "\n" + next["title"].toString() + " | " +
            display(next["start"].toString(), zone) + " — " +
            display(next["end"].toString(), zone) + "\n" + bs::tr("NextAction") +
            ": " + bs::tr(next["action"].toString().toUtf8().constData()) + " | " +
            display(next["time"].toString(), zone) + " (" +
            QString::number(-seconds) + " s)";
  }
  for (auto v : data["sync"].toArray()) {
    auto c = v.toObject();
    auto state = c["state"].toObject();
    text += "\n" + c["name"].toString() + ": " +
            bs::tr(!c["enabled"].toBool(true) ? "Disabled"
               : state.isEmpty()          ? "Pending"
               : state["ok"].toBool()     ? "SyncOK"
                                          : "SyncError");
  }
  dashboard->setText(text);
  QString selected;
  if (agenda->currentRow() >= 0)
    selected =
        agenda->item(agenda->currentRow(), 0)->data(Qt::UserRole).toString();
  agenda->setRowCount(0);
  QList<Event> ordered;
  for (auto v : data["events"].toArray()) {
    try {
      ordered.append(Event::parse(v.toObject()));
    } catch (const std::exception &e) {
      blog(LOG_ERROR, "[broadcast-scheduler] Ignoring invalid event in UI: %s",
           e.what());
    }
  }
  std::sort(ordered.begin(), ordered.end(),
            [](const Event &a, const Event &b) { return a.start < b.start; });
  for (auto &e : ordered) {
    row(agenda,
        {e.title, display(iso(e.start), zone), display(iso(e.end), zone),
         bs::tr(e.source.toUtf8().constData()),
         bs::tr(e.enabled ? "Enabled" : "Disabled")},
        e.id);
    if (e.id == selected)
      agenda->selectRow(agenda->rowCount() - 1);
  }
  history->setRowCount(0);
  for (auto v : data["history"].toArray()) {
    auto h = v.toObject();
    row(history,
        {h["title"].toString(), bs::tr(h["action"].toString().toUtf8().constData()),
         display(iso(qint64(h["scheduled"].toDouble())), zone),
         display(iso(qint64(h["actual"].toDouble())), zone),
         diagnostic(h["result"].toString()), diagnostic(h["message"].toString())});
  }
  logs->setRowCount(0);
  for (auto v : data["logs"].toArray()) {
    auto o = v.toObject();
    row(logs, {display(iso(qint64(o["time"].toDouble())), zone),
               diagnostic(o["level"].toString()), diagnostic(o["message"].toString())});
  }
  renderCalendar();
}
void Dock::renderCalendar() {
  calendarEvents->setRowCount(0);
  auto zone = deviceZone();
  auto date = calendar->selectedDate();
  auto mode = view->currentData().toString();
  auto begin = date, end = date.addDays(1);
  if (mode == "Week") {
    begin = date.addDays(1 - date.dayOfWeek());
    end = begin.addDays(7);
  }
  if (mode == "Month") {
    begin = QDate(date.year(), date.month(), 1);
    end = begin.addMonths(1);
  }
  for (auto v : current["events"].toArray()) {
    auto e = v.toObject();
    try {
      auto start =
          instant(e["start"].toString()).toTimeZone(QTimeZone(zone.toUtf8()));
      auto finish =
          instant(e["end"].toString()).toTimeZone(QTimeZone(zone.toUtf8()));
      if (mode == "Agenda" || (start.date() < end && finish.date() >= begin))
        row(calendarEvents,
            {e["title"].toString(), display(e["start"].toString(), zone),
             display(e["end"].toString(), zone)});
    } catch (const std::exception &ex) {
      blog(LOG_ERROR,
           "[broadcast-scheduler] Ignoring invalid calendar event in UI: %s",
           ex.what());
    }
  }
}
void Dock::eventDialog(QJsonObject e, bool duplicate) {
  const bool isNew = e.isEmpty();
  if (!e.isEmpty() && !duplicate && e["source"].toString() != "Manual" &&
      e["source"].toString() != "API") {
    QMessageBox::information(this, bs::tr("Edit"), bs::tr("ReadOnlyEvent"));
    return;
  }
  QDialog d(this);
  d.setWindowTitle(bs::tr("ScheduleRecording"));
  auto *l = new QVBoxLayout(&d);
  auto *f = new QFormLayout;
  l->addLayout(f);
  auto *title = new QLineEdit(e["title"].toString());
  auto *description = new QPlainTextEdit(e["description"].toString());
  description->setMaximumHeight(90);
  const auto zone = deviceZone();
  auto seed = QDateTime::currentDateTimeUtc().addSecs(120).toTimeZone(
      QTimeZone(zone.toUtf8()));
  auto *start = new QDateTimeEdit(
      isNew ? seed
                  : instant(e["start"].toString())
                        .toTimeZone(QTimeZone(zone.toUtf8())));
  auto *end = new QDateTimeEdit(
      isNew
          ? seed.addSecs(120)
          : instant(e["end"].toString()).toTimeZone(QTimeZone(zone.toUtf8())));
  for (auto *w : {start, end}) {
    w->setDisplayFormat("yyyy-MM-dd HH:mm:ss");
    w->setCalendarPopup(true);
  }
  auto *enabled = check(e["enabled"].toBool(true));
  f->addRow(bs::tr("Title"), title);
  f->addRow(bs::tr("Description"), description);
  f->addRow(bs::tr("Start"), start);
  f->addRow(bs::tr("End"), end);
  f->addRow(bs::tr("Enabled"), enabled);
  buttons(&d, l);
  while (d.exec() == QDialog::Accepted) {
    const auto z = QTimeZone::systemTimeZone();
    QDateTime s(start->date(), start->time(), z),
        finish(end->date(), end->time(), z);
    if (!z.isValid() || !s.isValid() || !finish.isValid() || finish <= s ||
        title->text().trimmed().isEmpty()) {
      QMessageBox::warning(this, bs::tr("Error"), bs::tr("InvalidEvent"));
      continue;
    }
    e["id"] = isNew || duplicate ? uid() : e.value("id").toString();
    e["title"] = title->text();
    e["description"] = description->toPlainText();
    e["start"] = iso(s.toMSecsSinceEpoch());
    e["end"] = iso(finish.toMSecsSinceEpoch());
    e["timezone"] = deviceZone();
    e.remove("template");
    e["enabled"] = enabled->isChecked();
    send("event.save", e);
    break;
  }
}
void Dock::calendars() {
  QDialog d(this);
  d.setWindowTitle(bs::tr("Calendars"));
  auto *l = new QVBoxLayout(&d);
  auto *hint = new QLabel(bs::tr("CalendarHelp"));
  hint->setWordWrap(true);
  l->addWidget(hint);
  auto *t = table({bs::tr("Enabled"), bs::tr("Title"), bs::tr("Provider"),
                   bs::tr("Location"), bs::tr("RefreshMinutes")});
  t->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
  l->addWidget(t);
  auto add = [&](const QJsonObject &c) {
    int r = t->rowCount();
    row(t, {"", c["name"].toString(), "", c["location"].toString(), ""},
        c["id"].toString(uid()));
    t->setCellWidget(r, 0, check(c["enabled"].toBool(true)));
    const bool managedOdoo = c["kind"].toString() == "odoo";
    t->setCellWidget(r, 2,
                     combo(managedOdoo ? QStringList{"odoo"}
                                       : QStringList{"ics", "file", "google"},
                           c["kind"].toString("ics")));
    if (managedOdoo) {
      for (const int column : {1, 3})
        t->item(r, column)->setFlags(t->item(r, column)->flags() &
                                     ~Qt::ItemIsEditable);
    }
    t->setCellWidget(r, 4, spin(c["refresh_minutes"].toInt(15), 1, 1440));
  };
  for (auto v : current["calendars"].toObject()["items"].toArray())
    add(v.toObject());
  auto *bar = new QHBoxLayout;
  l->addLayout(bar);
  button(bar, "AddCalendar", [&] { add({}); });
  button(bar, "ImportICS", [&] {
    auto path = QFileDialog::getOpenFileName(&d, bs::tr("ImportICS"), {}, "iCalendar (*.ics)");
    if (!path.isEmpty())
      add({{"kind", "file"}, {"name", QFileInfo(path).baseName()}, {"location", path}});
  });
  button(bar, "GoogleCalendars", [this] {
    send(current["google"].toObject()["connected"].toBool() ? "google.list" : "google.connect");
  });
  button(bar, "Delete", [&] {
    if (t->currentRow() >= 0) t->removeRow(t->currentRow());
  });
  buttons(&d, l);
  d.resize(880, 400);
  if (d.exec() == QDialog::Accepted) {
    QJsonArray out;
    for (int r = 0; r < t->rowCount(); ++r)
      out.append(QJsonObject{
          {"id", t->item(r, 0)->data(Qt::UserRole).toString()},
          {"enabled", static_cast<QCheckBox *>(t->cellWidget(r, 0))->isChecked()},
          {"name", t->item(r, 1)->text()},
          {"kind", static_cast<QComboBox *>(t->cellWidget(r, 2))->currentData().toString()},
          {"location", t->item(r, 3)->text()},
          {"refresh_minutes", static_cast<QSpinBox *>(t->cellWidget(r, 4))->value()}});
    send("calendars.save", {{"items", out}});
  }
}
void Dock::recurrences() {
  QDialog d(this);
  d.setWindowTitle(bs::tr("Recurrences"));
  auto *l = new QVBoxLayout(&d);
  auto *hint = new QLabel(bs::tr("SimpleRecurrenceHelp"));
  hint->setWordWrap(true);
  l->addWidget(hint);
  auto *t = table({bs::tr("Title"), bs::tr("Start"), bs::tr("End"),
                   bs::tr("Repeat"), bs::tr("Enabled")});
  t->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
  l->addWidget(t);
  auto add = [&](const QJsonObject &o) {
    const auto e = o["event"].toObject();
    int r = t->rowCount();
    row(t, {e["title"].toString(), "", "", "", ""}, e["id"].toString(uid()));
    auto start = e.isEmpty() ? QDateTime::currentDateTime().addSecs(120)
                            : instant(e["start"].toString()).toLocalTime();
    auto end = e.isEmpty() ? start.addSecs(3600) : instant(e["end"].toString()).toLocalTime();
    for (int col : {1, 2}) {
      auto *date = new QDateTimeEdit(col == 1 ? start : end);
      date->setDisplayFormat("yyyy-MM-dd HH:mm");
      date->setCalendarPopup(true);
      t->setCellWidget(r, col, date);
    }
    auto *repeat = new QComboBox;
    repeat->addItem(bs::tr("Daily"), "FREQ=DAILY");
    repeat->addItem(bs::tr("Weekly"), "FREQ=WEEKLY");
    repeat->addItem(bs::tr("Weekdays"), "FREQ=WEEKLY;BYDAY=MO,TU,WE,TH,FR");
    repeat->addItem(bs::tr("Monthly"), "FREQ=MONTHLY");
    const auto rule = o["rrule"].toString("FREQ=WEEKLY");
    int selected = repeat->findData(rule);
    if (selected < 0) {
      repeat->addItem(bs::tr("ExistingRecurrence"), rule);
      selected = repeat->count() - 1;
    }
    repeat->setCurrentIndex(selected);
    t->setCellWidget(r, 3, repeat);
    t->setCellWidget(r, 4, check(e["enabled"].toBool(true)));
  };
  for (auto v : current["recurrences"].toObject()["items"].toArray()) add(v.toObject());
  auto *bar = new QHBoxLayout;
  l->addLayout(bar);
  button(bar, "New", [&] { add({}); });
  button(bar, "Delete", [&] { if (t->currentRow() >= 0) t->removeRow(t->currentRow()); });
  buttons(&d, l);
  d.resize(980, 400);
  while (d.exec() == QDialog::Accepted) {
    try {
      QJsonArray out;
      for (int r = 0; r < t->rowCount(); ++r) {
        auto localTime = [&](int col) {
          auto *w = static_cast<QDateTimeEdit *>(t->cellWidget(r, col));
          return iso(deviceInstant(w->dateTime().toString("yyyy-MM-dd'T'HH:mm:ss")).toMSecsSinceEpoch());
        };
        auto e = Event::parse({
          {"id", t->item(r, 0)->data(Qt::UserRole).toString()},
          {"title", t->item(r, 0)->text()},
          {"start", localTime(1)}, {"end", localTime(2)},
          {"timezone", deviceZone()},
          {"enabled", static_cast<QCheckBox *>(t->cellWidget(r, 4))->isChecked()}});
        out.append(QJsonObject{{"event", e.json()},
          {"rrule", static_cast<QComboBox *>(t->cellWidget(r, 3))->currentData().toString()}});
      }
      send("recurrences.save", {{"items", out}});
      break;
    } catch (const std::exception &e) {
      QMessageBox::warning(&d, bs::tr("Error"), diagnostic(QString::fromUtf8(e.what())));
    }
  }
}
void Dock::settings() {
  QDialog d(this);
  d.setWindowTitle(bs::tr("Settings"));
  auto *l = new QVBoxLayout(&d);
  auto *tabs = new QTabWidget;
  l->addWidget(tabs);
  auto page = [&](const char *key) {
    auto *w = new QWidget;
    auto *f = new QFormLayout(w);
    f->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(w);
    tabs->addTab(scroll, bs::tr(key));
    return f;
  };
  auto s = current["settings"].toObject();
  auto *f = page("General");
  auto *enabled = check(s["enabled"].toBool(true));
  f->addRow(bs::tr("Enabled"), enabled);
  auto *local = new QLabel(bs::tr("DeviceTimeHelp") + " (" + deviceZone() + ")");
  local->setWordWrap(true);
  f->addRow(local);
  auto *safety = new QLabel(bs::tr("RecordingSafetyHelp"));
  safety->setWordWrap(true);
  f->addRow(safety);
  f = page("Google");
  const auto google = current["google"].toObject();
  const auto storedGoogleClientId = google["client_id"].toString();
  const bool storedGoogleClientSecret =
      google["client_secret_configured"].toBool();
  auto *googleClientId = new QLineEdit(google["client_id"].toString());
  googleClientId->setPlaceholderText("000000000000-example.apps.googleusercontent.com");
  googleClientId->setValidator(new QRegularExpressionValidator(
      QRegularExpression("[A-Za-z0-9_.-]+\\.apps\\.googleusercontent\\.com"),
      googleClientId));
  f->addRow(bs::tr("ClientID"), googleClientId);
  auto *googleClientSecret = new QLineEdit;
  googleClientSecret->setEchoMode(QLineEdit::Password);
  googleClientSecret->setPlaceholderText(
      google["client_secret_configured"].toBool()
          ? bs::tr("ClientSecretStored")
          : "GOCSPX-…");
  f->addRow(bs::tr("ClientSecret"), googleClientSecret);
  auto *googleState = new QLabel;
  googleState->setWordWrap(true);
  f->addRow(googleState);
  auto *googleHelp = new QLabel(bs::tr("GoogleConnectHelp"));
  googleHelp->setWordWrap(true);
  f->addRow(googleHelp);
  auto *googleGuide = new QLabel(bs::tr("GoogleSetupGuide"));
  googleGuide->setTextFormat(Qt::RichText);
  googleGuide->setTextInteractionFlags(Qt::TextBrowserInteraction);
  googleGuide->setOpenExternalLinks(false);
  googleGuide->setWordWrap(true);
  connect(googleGuide, &QLabel::linkActivated, googleGuide,
          [this](const QString &link) {
            const QUrl url(link);
            if (url.scheme() != "https" || !QDesktopServices::openUrl(url))
              QMessageBox::warning(this, bs::tr("Google"),
                                   bs::tr("LinkOpenFailed"));
          });
  f->addRow(googleGuide);
  auto *googleButtons = new QHBoxLayout;
  f->addRow(googleButtons);
  auto configureGoogle = [this, googleClientId, googleClientSecret](bool required) {
    const auto clientId = googleClientId->text().trimmed();
    const auto clientSecret = googleClientSecret->text().trimmed();
    const auto status = current["google"].toObject();
    const bool idChanged = clientId != status["client_id"].toString();
    const bool hasSecret = !clientSecret.isEmpty() ||
                           (!idChanged && status["client_secret_configured"].toBool());
    if ((required && clientId.isEmpty()) ||
        (!clientId.isEmpty() && !googleClientId->hasAcceptableInput())) {
      QMessageBox::warning(this, bs::tr("Google"),
                           bs::tr("InvalidGoogleClientID"));
      return false;
    }
    if (required && !hasSecret) {
      QMessageBox::warning(this, bs::tr("Google"),
                           bs::tr("MissingGoogleClientSecret"));
      return false;
    }
    if (idChanged || !clientSecret.isEmpty()) {
      if (status["connected"].toBool() &&
          QMessageBox::question(this, bs::tr("ChangeGoogleClientID"),
                                bs::tr("ChangeGoogleClientIDHelp")) !=
              QMessageBox::Yes)
        return false;
      send("google.configure", {{"client_id", clientId},
                                {"client_secret", clientSecret}});
    }
    return true;
  };
  auto *connectGoogle = button(googleButtons, "ConnectGoogle", [this, googleClientId, googleClientSecret, configureGoogle] {
    const auto status = current["google"].toObject();
    const bool changed = googleClientId->text().trimmed() !=
                             status["client_id"].toString() ||
                         !googleClientSecret->text().trimmed().isEmpty();
    if (!configureGoogle(true))
      return;
    if (status["connected"].toBool() && !changed) {
      if (QMessageBox::question(this, bs::tr("ChangeGoogleAccount"),
            bs::tr("ChangeGoogleAccountHelp")) != QMessageBox::Yes) return;
      send("google.disconnect");
    }
    send("google.connect");
  });
  auto *disconnectGoogle = button(googleButtons, "Disconnect", [this] { send("google.disconnect"); });
  auto *chooseGoogle = button(googleButtons, "GoogleCalendars", [this] { send("google.list"); });
  auto updateGoogle = [googleState, googleClientId, googleClientSecret, connectGoogle,
                       disconnectGoogle, chooseGoogle](QJsonObject data) {
    const auto g = data["google"].toObject();
    const bool configured = g["configured"].toBool();
    const bool connected = g["connected"].toBool();
    const bool connecting = g["connecting"].toBool();
    googleState->setText(bs::tr(!configured ? "GoogleNotConfigured" : g["storage_error"].toBool() ? "GoogleStorageError" : connecting ? "GoogleConnecting" : connected ? "GoogleConnected" : "GoogleDisconnected"));
    connectGoogle->setText(bs::tr(connected ? "ChangeGoogleAccount" : "ConnectGoogle"));
    connectGoogle->setEnabled(!connecting &&
                              !googleClientId->text().trimmed().isEmpty() &&
                              googleClientId->hasAcceptableInput() &&
                              (g["client_secret_configured"].toBool() ||
                               !googleClientSecret->text().trimmed().isEmpty()));
    disconnectGoogle->setText(bs::tr(connecting ? "CancelGoogleConnection" : "Disconnect"));
    disconnectGoogle->setEnabled(connected || connecting);
    chooseGoogle->setEnabled(connected && !connecting);
  };
  connect(runtime, &Runtime::state, &d, updateGoogle);
  connect(googleClientId, &QLineEdit::textChanged, &d,
          [googleClientId, googleClientSecret, connectGoogle,
           storedGoogleClientId, storedGoogleClientSecret](const QString &) {
            const bool hasSecret = !googleClientSecret->text().trimmed().isEmpty() ||
                (googleClientId->text().trimmed() == storedGoogleClientId &&
                 storedGoogleClientSecret);
            connectGoogle->setEnabled(
                !googleClientId->text().trimmed().isEmpty() &&
                googleClientId->hasAcceptableInput() &&
                hasSecret);
          });
  connect(googleClientSecret, &QLineEdit::textChanged, &d,
          [googleClientId, googleClientSecret, connectGoogle,
           storedGoogleClientId, storedGoogleClientSecret](const QString &) {
            const bool hasSecret = !googleClientSecret->text().trimmed().isEmpty() ||
                (googleClientId->text().trimmed() == storedGoogleClientId &&
                 storedGoogleClientSecret);
            connectGoogle->setEnabled(
                !googleClientId->text().trimmed().isEmpty() &&
                googleClientId->hasAcceptableInput() &&
                hasSecret);
          });
  updateGoogle(current);
  f = page("Odoo");
  const auto odoo = current["odoo"].toObject();
  auto *odooEnabled = check(odoo["enabled"].toBool(true));
  auto *odooUrl = new QLineEdit(odoo["url"].toString());
  odooUrl->setPlaceholderText("https://odoo.example.com");
  auto *odooDatabase = new QLineEdit(odoo["database"].toString());
  auto *odooLogin = new QLineEdit(odoo["login"].toString());
  auto *odooProtocol = combo({"jsonrpc", "json2"},
                             odoo["protocol"].toString("jsonrpc"));
  auto *odooApiKey = new QLineEdit;
  odooApiKey->setEchoMode(QLineEdit::Password);
  odooApiKey->setPlaceholderText(
      odoo["api_key_configured"].toBool()
          ? bs::tr("ApiKeyStored")
          : bs::tr("OdooApiKeyPlaceholder"));
  auto *odooRefresh = spin(odoo["refresh_minutes"].toInt(15), 1, 1440);
  f->addRow(bs::tr("Enabled"), odooEnabled);
  f->addRow(bs::tr("OdooURL"), odooUrl);
  f->addRow(bs::tr("OdooDatabase"), odooDatabase);
  f->addRow(bs::tr("OdooLogin"), odooLogin);
  f->addRow(bs::tr("OdooProtocol"), odooProtocol);
  f->addRow(bs::tr("OdooApiKey"), odooApiKey);
  f->addRow(bs::tr("RefreshMinutes"), odooRefresh);
  auto *odooHelp = new QLabel(bs::tr("OdooConnectHelp"));
  odooHelp->setWordWrap(true);
  f->addRow(odooHelp);
  auto *odooState = new QLabel;
  odooState->setWordWrap(true);
  f->addRow(odooState);
  auto odooConfiguration = [=] {
    QJsonObject configuration = odoo;
    configuration.remove("configured");
    configuration.remove("api_key_configured");
    configuration.remove("storage_error");
    configuration["url"] = odooUrl->text().trimmed();
    configuration["database"] = odooDatabase->text().trimmed();
    configuration["login"] = odooLogin->text().trimmed();
    configuration["protocol"] = odooProtocol->currentData().toString();
    configuration["enabled"] = odooEnabled->isChecked();
    configuration["refresh_minutes"] = odooRefresh->value();
    return configuration;
  };
  auto configureOdoo = [this, odoo, odooApiKey, odooConfiguration](bool required) {
    const auto configuration = odooConfiguration();
    if (!required && configuration["url"].toString().isEmpty() &&
        odoo["url"].toString().isEmpty() &&
        odooApiKey->text().trimmed().isEmpty())
      return true;
    const bool legacy = configuration["protocol"].toString() == "jsonrpc";
    if (required && (configuration["url"].toString().isEmpty() ||
                     configuration["database"].toString().isEmpty() ||
                     (legacy && configuration["login"].toString().isEmpty()))) {
      QMessageBox::warning(this, bs::tr("Odoo"), bs::tr("InvalidOdooSettings"));
      return false;
    }
    const bool identityChanged = configuration["url"] != odoo["url"] ||
        configuration["database"] != odoo["database"] ||
        configuration["login"] != odoo["login"] ||
        configuration["protocol"] != odoo["protocol"];
    if (required && odooApiKey->text().trimmed().isEmpty() &&
        (identityChanged || !odoo["api_key_configured"].toBool())) {
      QMessageBox::warning(this, bs::tr("Odoo"), bs::tr("MissingOdooApiKey"));
      return false;
    }
    QJsonObject comparable = odoo;
    comparable.remove("configured");
    comparable.remove("api_key_configured");
    comparable.remove("storage_error");
    if (configuration != comparable || !odooApiKey->text().trimmed().isEmpty())
      send("odoo.configure", {{"configuration", configuration},
                              {"api_key", odooApiKey->text().trimmed()}});
    return true;
  };
  auto *odooButtons = new QHBoxLayout;
  f->addRow(odooButtons);
  auto *connectOdoo = button(odooButtons, "ConnectOdoo", [this, configureOdoo] {
    if (configureOdoo(true))
      send("odoo.list");
  });
  auto *disconnectOdoo = button(odooButtons, "Disconnect", [this] {
    send("odoo.disconnect");
  });
  auto updateOdoo = [odooState, connectOdoo, disconnectOdoo](QJsonObject data) {
    const auto status = data["odoo"].toObject();
    odooState->setText(bs::tr(status["storage_error"].toBool()
                                  ? "OdooStorageError"
                                  : status["configured"].toBool()
                                        ? "OdooConfigured"
                                        : "OdooNotConfigured"));
    disconnectOdoo->setEnabled(!status["url"].toString().isEmpty() ||
                               status["api_key_configured"].toBool());
    connectOdoo->setEnabled(!status["storage_error"].toBool());
  };
  connect(runtime, &Runtime::state, &d, updateOdoo);
  updateOdoo(current);
  f = page("API");
  auto *apiEnabled = check(s["api_enabled"].toBool());
  auto *host = new QLineEdit(s["api_host"].toString("127.0.0.1"));
  host->setText("127.0.0.1");
  host->setReadOnly(true);
  auto *apiHelp = new QLabel(bs::tr("ApiLocalOnly"));
  apiHelp->setWordWrap(true);
  f->addRow(apiHelp);
  auto *port = spin(s["api_port"].toInt(8766), 1024, 65535);
  f->addRow(bs::tr("Enabled"), apiEnabled);
  f->addRow(bs::tr("Host"), host);
  f->addRow(bs::tr("Port"), port);
  auto *token = new QPushButton(bs::tr("RegenerateToken"));
  f->addRow(token);
  connect(token, &QPushButton::clicked, this,
          [this] { send("token.generate"); });
  f = page("Diagnostics");
  f->addRow(bs::tr("Version"), new QLabel(PLUGIN_VERSION));
  auto *db = new QLineEdit(current["database"].toString());
  db->setReadOnly(true);
  f->addRow(bs::tr("Database"), db);
  auto *open = new QPushButton(bs::tr("OpenDataFolder"));
  f->addRow(open);
  connect(open, &QPushButton::clicked, this, [this] {
    QDesktopServices::openUrl(QUrl::fromLocalFile(
        QFileInfo(current["database"].toString()).absolutePath()));
  });
  buttons(&d, l);
  d.resize(660, 440);
  if (d.exec() == QDialog::Accepted) {
    if (!configureGoogle(false))
      return;
    if (!configureOdoo(false))
      return;
    send("settings.save",
         {{"enabled", enabled->isChecked()},
          {"api_enabled", apiEnabled->isChecked()},
          {"api_host", host->text()},
          {"api_port", port->value()}});
  }
}
} // namespace bs
