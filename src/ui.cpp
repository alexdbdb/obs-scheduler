#include "ui.hpp"
#include <QCheckBox>
#include <QDateTimeEdit>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonDocument>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <obs-module.h>
namespace bs {
QString tr(const char *key) { return QString::fromUtf8(obs_module_text(key)); }
namespace {
QPushButton *button(QBoxLayout *l, const char *key, std::function<void()> fn) {
  auto *b = new QPushButton(tr(key));
  l->addWidget(b);
  QObject::connect(b, &QPushButton::clicked, b, fn);
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
  for (auto &s : choices)
    c->addItem(tr(s.toUtf8().constData()), s);
  int i = c->findData(selected);
  if (i >= 0)
    c->setCurrentIndex(i);
  return c;
}
QComboBox *templateBox(const QJsonObject &state, QString selected) {
  auto *c = new QComboBox;
  for (auto v : state["templates"].toArray()) {
    auto t = v.toObject();
    c->addItem(t["name"].toString(), t["id"].toString());
  }
  int i = c->findData(selected);
  if (i >= 0)
    c->setCurrentIndex(i);
  return c;
}
QComboBox *timezoneBox(QString selected) {
  auto *c = new QComboBox;
  c->setEditable(true);
  for (auto &z : QTimeZone::availableTimeZoneIds())
    c->addItem(QString::fromUtf8(z));
  c->setCurrentText(selected);
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
  button(bar, "AddEvent", [this] { eventDialog(); });
  button(bar, "Templates", [this] { templates(); });
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
  agenda = table({tr("Title"), tr("Start"), tr("End"), tr("Template"),
                  tr("Source"), tr("Enabled")});
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
  button(actions, "Delete", [this, selected] {
    auto e = selected();
    if (!e.isEmpty() &&
        QMessageBox::question(this, tr("Delete"), tr("DeleteEvent")) ==
            QMessageBox::Yes)
      send("event.delete", e);
  });
  tabs->addTab(up, tr("Upcoming"));
  auto *cp = new QWidget;
  auto *cl = new QVBoxLayout(cp);
  view = combo({"Agenda", "Day", "Week", "Month"}, "Month");
  cl->addWidget(view);
  calendar = new QCalendarWidget;
  cl->addWidget(calendar);
  calendarEvents = table({tr("Title"), tr("Start"), tr("End")});
  cl->addWidget(calendarEvents);
  connect(calendar, &QCalendarWidget::selectionChanged, this,
          &Dock::renderCalendar);
  connect(view, &QComboBox::currentIndexChanged, this, &Dock::renderCalendar);
  tabs->addTab(cp, tr("Calendar"));
  history = table({tr("Title"), tr("Action"), tr("Scheduled"), tr("Actual"),
                   tr("Result"), tr("Message")});
  tabs->addTab(history, tr("History"));
  logs = table({tr("Actual"), tr("Result"), tr("Message")});
  tabs->addTab(logs, tr("Logs"));
  auto *emergency = new QHBoxLayout;
  l->addLayout(emergency);
  for (auto item :
       QList<QPair<const char *, QString>>{{"StartRecording", "record.start"},
                                           {"StopRecording", "record.stop"},
                                           {"StartStreaming", "stream.start"},
                                           {"StopStreaming", "stream.stop"}})
    button(emergency, item.first, [this, item] {
      if (item.second.endsWith("stop") &&
          QMessageBox::warning(this, tr(item.first), tr("ConfirmStop"),
                               QMessageBox::Yes | QMessageBox::No,
                               QMessageBox::No) != QMessageBox::Yes)
        return;
      send("action", {{"type", item.second}});
    });
  connect(runtime, &Runtime::state, this, &Dock::updateState);
  connect(runtime, &Runtime::problem, this,
          [this](QString m) { QMessageBox::warning(this, tr("Error"), m); });
  connect(runtime, &Runtime::ask, this, &Dock::safetyQuestion);
  connect(runtime, &Runtime::openUrl, this,
          [](QString u) { QDesktopServices::openUrl(QUrl(u)); });
  connect(runtime, &Runtime::tokenGenerated, this, [this](QString t) {
    QDialog d(this);
    d.setWindowTitle(tr("ApiToken"));
    auto *l = new QVBoxLayout(&d);
    auto *label = new QLabel(tr("TokenOnce"));
    label->setWordWrap(true);
    l->addWidget(label);
    auto *line = new QLineEdit(t);
    line->setReadOnly(true);
    l->addWidget(line);
    buttons(&d, l);
    d.exec();
  });
  connect(runtime, &Runtime::googleCalendars, this, [this](QJsonArray list) {
    QDialog d(this);
    d.setWindowTitle(tr("GoogleCalendars"));
    auto *l = new QVBoxLayout(&d);
    auto *t =
        table({tr("Enabled"), tr("Title"), tr("Template"), tr("Timezone")});
    l->addWidget(t);
    auto existing = current["calendars"].toObject()["items"].toArray();
    for (auto v : list) {
      auto c = v.toObject();
      QJsonObject saved;
      for (auto x : existing)
        if (x.toObject()["kind"].toString() == "google" &&
            x.toObject()["location"] == c["id"])
          saved = x.toObject();
      row(t, {"", c["summary"].toString(), "", c["timeZone"].toString("UTC")},
          c["id"].toString());
      int r = t->rowCount() - 1;
      t->setCellWidget(r, 0, check(saved["enabled"].toBool(false)));
      t->setCellWidget(r, 2,
                       templateBox(current, saved["template"].toString()));
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
               {"timezone", t->item(i, 3)->text()},
               {"refresh_minutes", 15}};
        c["enabled"] =
            static_cast<QCheckBox *>(t->cellWidget(i, 0))->isChecked();
        c["template"] = static_cast<QComboBox *>(t->cellWidget(i, 2))
                            ->currentData()
                            .toString();
        out.append(c);
      }
      send("calendars.save", {{"items", out}});
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
  auto zone = s["timezone"].toString("UTC");
  auto obs = data["obs"].toObject();
  auto next = data["next"].toObject();
  QString text =
      tr("Recording") + ": " +
      tr(obs["recording"].toBool() ? "Active" : "Inactive") + "   " +
      tr("Streaming") + ": " +
      tr(obs["streaming"].toBool() ? "Active" : "Inactive") + "\n" +
      tr("Scheduler") + ": " +
      tr(s["enabled"].toBool(true) ? "Running" : "Paused") +
      "   API: " + tr(data["api_running"].toBool() ? "Running" : "Disabled") +
      "   " + zone;
  if (!next.isEmpty()) {
    auto seconds =
        now() / 1000 - instant(next["time"].toString()).toSecsSinceEpoch();
    text += "\n" + next["title"].toString() + " | " +
            display(next["start"].toString(), zone) + " — " +
            display(next["end"].toString(), zone) + "\n" + tr("NextAction") +
            ": " + tr(next["action"].toString().toUtf8().constData()) + " | " +
            display(next["time"].toString(), zone) + " (" +
            QString::number(-seconds) + " s)";
  }
  for (auto v : data["sync"].toArray()) {
    auto c = v.toObject();
    auto state = c["state"].toObject();
    text += "\n" + c["name"].toString() + ": " +
            tr(!c["enabled"].toBool(true) ? "Disabled"
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
  for (auto v : data["events"].toArray())
    ordered.append(Event::parse(v.toObject()));
  std::sort(ordered.begin(), ordered.end(),
            [](const Event &a, const Event &b) { return a.start < b.start; });
  for (auto &e : ordered) {
    row(agenda,
        {e.title, display(iso(e.start), zone), display(iso(e.end), zone),
         e.templateId, tr(e.source.toUtf8().constData()),
         tr(e.enabled ? "Enabled" : "Disabled")},
        e.id);
    if (e.id == selected)
      agenda->selectRow(agenda->rowCount() - 1);
  }
  history->setRowCount(0);
  for (auto v : data["history"].toArray()) {
    auto h = v.toObject();
    row(history,
        {h["title"].toString(), tr(h["action"].toString().toUtf8().constData()),
         display(iso(qint64(h["scheduled"].toDouble())), zone),
         display(iso(qint64(h["actual"].toDouble())), zone),
         h["result"].toString(), h["message"].toString()});
  }
  logs->setRowCount(0);
  for (auto v : data["logs"].toArray()) {
    auto o = v.toObject();
    row(logs, {display(iso(qint64(o["time"].toDouble())), zone),
               o["level"].toString(), o["message"].toString()});
  }
  renderCalendar();
}
void Dock::renderCalendar() {
  calendarEvents->setRowCount(0);
  auto zone = current["settings"].toObject()["timezone"].toString("UTC");
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
    auto start =
        instant(e["start"].toString()).toTimeZone(QTimeZone(zone.toUtf8()));
    auto finish =
        instant(e["end"].toString()).toTimeZone(QTimeZone(zone.toUtf8()));
    if (mode == "Agenda" || (start.date() < end && finish.date() >= begin))
      row(calendarEvents,
          {e["title"].toString(), display(e["start"].toString(), zone),
           display(e["end"].toString(), zone)});
  }
}
void Dock::eventDialog(QJsonObject e, bool duplicate) {
  if (!e.isEmpty() && !duplicate && e["source"].toString() != "Manual" &&
      e["source"].toString() != "API") {
    QMessageBox::information(this, tr("Edit"), tr("ReadOnlyEvent"));
    return;
  }
  QDialog d(this);
  d.setWindowTitle(tr("Event"));
  auto *l = new QVBoxLayout(&d);
  auto *f = new QFormLayout;
  l->addLayout(f);
  auto *title = new QLineEdit(e["title"].toString());
  auto *description = new QPlainTextEdit(e["description"].toString());
  description->setMaximumHeight(90);
  auto zone = e["timezone"].toString(
      current["settings"].toObject()["timezone"].toString("UTC"));
  auto *tz = timezoneBox(zone);
  auto seed = QDateTime::currentDateTimeUtc().addSecs(120).toTimeZone(
      QTimeZone(zone.toUtf8()));
  auto *start = new QDateTimeEdit(
      e.isEmpty() ? seed
                  : instant(e["start"].toString())
                        .toTimeZone(QTimeZone(zone.toUtf8())));
  auto *end = new QDateTimeEdit(
      e.isEmpty()
          ? seed.addSecs(120)
          : instant(e["end"].toString()).toTimeZone(QTimeZone(zone.toUtf8())));
  for (auto *w : {start, end}) {
    w->setDisplayFormat("yyyy-MM-dd HH:mm:ss");
    w->setCalendarPopup(true);
  }
  auto *t = templateBox(current, e["template"].toString("Recording Only"));
  auto *enabled = check(e["enabled"].toBool(true));
  f->addRow(tr("Title"), title);
  f->addRow(tr("Description"), description);
  f->addRow(tr("Start"), start);
  f->addRow(tr("End"), end);
  f->addRow(tr("Timezone"), tz);
  f->addRow(tr("Template"), t);
  f->addRow(tr("Enabled"), enabled);
  buttons(&d, l);
  while (d.exec() == QDialog::Accepted) {
    QTimeZone z(tz->currentText().toUtf8());
    QDateTime s(start->date(), start->time(), z),
        finish(end->date(), end->time(), z);
    if (!z.isValid() || !s.isValid() || !finish.isValid() || finish <= s ||
        title->text().trimmed().isEmpty()) {
      QMessageBox::warning(this, tr("Error"), tr("InvalidEvent"));
      continue;
    }
    e["id"] = e.isEmpty() || duplicate ? uid() : e["id"].toString();
    e["title"] = title->text();
    e["description"] = description->toPlainText();
    e["start"] = iso(s.toMSecsSinceEpoch());
    e["end"] = iso(finish.toMSecsSinceEpoch());
    e["timezone"] = tz->currentText();
    e["template"] = t->currentData().toString();
    e["enabled"] = enabled->isChecked();
    send("event.save", e);
    break;
  }
}
void Dock::templates() {
  QDialog d(this);
  d.setWindowTitle(tr("Templates"));
  auto *l = new QVBoxLayout(&d);
  auto *select = templateBox(current, {});
  l->addWidget(select);
  auto *name = new QLineEdit;
  l->addWidget(name);
  auto *t = table(
      {tr("Reference"), tr("OffsetSeconds"), tr("Action"), tr("Parameters")});
  t->setEditTriggers(QAbstractItemView::DoubleClicked |
                     QAbstractItemView::EditKeyPressed);
  l->addWidget(t);
  QString id;
  auto add = [t](Action a) {
    int r = t->rowCount();
    row(t,
        {"", "", "",
         QString::fromUtf8(
             QJsonDocument(a.parameters).toJson(QJsonDocument::Compact))},
        a.id);
    t->setCellWidget(r, 0, combo({"start", "end"}, a.reference));
    t->setCellWidget(r, 1, spin(int(a.offset), -31622400, 31622400));
    t->setCellWidget(r, 2, combo(actionTypes(), a.type));
  };
  auto load = [&] {
    t->setRowCount(0);
    id = select->currentData().toString();
    for (auto v : current["templates"].toArray()) {
      auto obj = v.toObject();
      if (obj["id"].toString() != id)
        continue;
      auto temp = Template::parse(obj);
      name->setText(temp.name);
      std::stable_sort(temp.actions.begin(), temp.actions.end(),
                       [](const Action &a, const Action &b) {
                         if (a.reference == b.reference)
                           return a.offset < b.offset;
                         return a.reference == "start";
                       });
      for (auto &a : temp.actions)
        add(a);
    }
  };
  connect(select, &QComboBox::currentIndexChanged, &d, load);
  load();
  auto *b = new QHBoxLayout;
  l->addLayout(b);
  button(b, "New", [&] {
    id = uid();
    name->clear();
    t->setRowCount(0);
  });
  button(b, "AddAction",
         [&] { add(Action{uid(), "record.start", "start", 0, {}}); });
  button(b, "Delete", [&] {
    if (t->currentRow() >= 0)
      t->removeRow(t->currentRow());
  });
  auto *hint = new QLabel(tr("ParametersHelp"));
  hint->setWordWrap(true);
  l->addWidget(hint);
  buttons(&d, l);
  d.resize(850, 500);
  while (d.exec() == QDialog::Accepted) {
    try {
      Template temp{id, name->text(), {}};
      for (int i = 0; i < t->rowCount(); ++i) {
        QJsonParseError err;
        auto params =
            QJsonDocument::fromJson(t->item(i, 3)->text().toUtf8(), &err);
        if (err.error != QJsonParseError::NoError || !params.isObject())
          throw Error(tr("InvalidJSON"));
        temp.actions.append(
            Action{t->item(i, 0)->data(Qt::UserRole).toString(),
                   static_cast<QComboBox *>(t->cellWidget(i, 2))
                       ->currentData()
                       .toString(),
                   static_cast<QComboBox *>(t->cellWidget(i, 0))
                       ->currentData()
                       .toString(),
                   static_cast<QSpinBox *>(t->cellWidget(i, 1))->value(),
                   params.object()});
      }
      send("template.save", Template::parse(temp.json()).json());
      break;
    } catch (const std::exception &e) {
      QMessageBox::warning(this, tr("Error"), QString::fromUtf8(e.what()));
    }
  }
}
void Dock::calendars() {
  QDialog d(this);
  d.setWindowTitle(tr("Calendars"));
  auto *l = new QVBoxLayout(&d);
  auto *t = table({tr("Enabled"), tr("Title"), tr("Provider"), tr("Location"),
                   tr("Template"), tr("RefreshMinutes"), tr("StartOffset"),
                   tr("EndOffset"), tr("Timezone")});
  t->setEditTriggers(QAbstractItemView::DoubleClicked |
                     QAbstractItemView::EditKeyPressed);
  l->addWidget(t);
  auto add = [&](QJsonObject c) {
    int r = t->rowCount();
    row(t,
        {"", c["name"].toString(), "", c["location"].toString(), "", "", "", "",
         c["timezone"].toString("UTC")},
        c["id"].toString(uid()));
    t->setCellWidget(r, 0, check(c["enabled"].toBool(true)));
    t->setCellWidget(
        r, 2, combo({"ics", "file", "google"}, c["kind"].toString("ics")));
    t->setCellWidget(r, 4, templateBox(current, c["template"].toString()));
    t->setCellWidget(r, 5, spin(c["refresh_minutes"].toInt(15), 1, 1440));
    t->setCellWidget(r, 6, spin(c["start_offset"].toInt(), -86400, 86400));
    t->setCellWidget(r, 7, spin(c["end_offset"].toInt(), -86400, 86400));
  };
  for (auto v : current["calendars"].toObject()["items"].toArray())
    add(v.toObject());
  auto *b = new QHBoxLayout;
  l->addLayout(b);
  button(b, "AddCalendar", [&] { add({}); });
  button(b, "ImportICS", [&] {
    auto path = QFileDialog::getOpenFileName(&d, tr("ImportICS"), {},
                                             "iCalendar (*.ics)");
    if (!path.isEmpty())
      add({{"kind", "file"},
           {"name", QFileInfo(path).baseName()},
           {"location", path}});
  });
  button(b, "Delete", [&] {
    if (t->currentRow() >= 0)
      t->removeRow(t->currentRow());
  });
  buttons(&d, l);
  d.resize(1000, 400);
  if (d.exec() == QDialog::Accepted) {
    QJsonArray out;
    for (int r = 0; r < t->rowCount(); ++r)
      out.append(QJsonObject{
          {"id", t->item(r, 0)->data(Qt::UserRole).toString()},
          {"enabled",
           static_cast<QCheckBox *>(t->cellWidget(r, 0))->isChecked()},
          {"name", t->item(r, 1)->text()},
          {"kind", static_cast<QComboBox *>(t->cellWidget(r, 2))
                       ->currentData()
                       .toString()},
          {"location", t->item(r, 3)->text()},
          {"template", static_cast<QComboBox *>(t->cellWidget(r, 4))
                           ->currentData()
                           .toString()},
          {"refresh_minutes",
           static_cast<QSpinBox *>(t->cellWidget(r, 5))->value()},
          {"start_offset",
           static_cast<QSpinBox *>(t->cellWidget(r, 6))->value()},
          {"end_offset", static_cast<QSpinBox *>(t->cellWidget(r, 7))->value()},
          {"timezone", t->item(r, 8)->text()}});
    send("calendars.save", {{"items", out}});
  }
}
void Dock::recurrences() {
  QDialog d(this);
  d.setWindowTitle(tr("Recurrences"));
  auto *l = new QVBoxLayout(&d);
  auto *hint = new QLabel(tr("RecurrenceHelp"));
  hint->setWordWrap(true);
  l->addWidget(hint);
  auto *t = table({tr("Title"), tr("Start"), tr("End"), tr("Timezone"),
                   tr("Template"), tr("RRULE"), tr("Enabled")});
  t->setEditTriggers(QAbstractItemView::DoubleClicked |
                     QAbstractItemView::EditKeyPressed);
  l->addWidget(t);
  auto add = [&](QJsonObject o) {
    auto e = o["event"].toObject();
    auto start = now() + 120000;
    int r = t->rowCount();
    row(t,
        {e["title"].toString(), e["start"].toString(iso(start)),
         e["end"].toString(iso(start + 7200000)), e["timezone"].toString("UTC"),
         "", o["rrule"].toString("FREQ=WEEKLY;BYDAY=FR"), ""},
        e["id"].toString(uid()));
    t->setCellWidget(r, 4, templateBox(current, e["template"].toString()));
    t->setCellWidget(r, 6, check(e["enabled"].toBool(true)));
  };
  for (auto v : current["recurrences"].toObject()["items"].toArray())
    add(v.toObject());
  auto *b = new QHBoxLayout;
  l->addLayout(b);
  button(b, "New", [&] { add({}); });
  button(b, "Delete", [&] {
    if (t->currentRow() >= 0)
      t->removeRow(t->currentRow());
  });
  buttons(&d, l);
  d.resize(1050, 400);
  while (d.exec() == QDialog::Accepted) {
    try {
      QJsonArray out;
      for (int r = 0; r < t->rowCount(); ++r) {
        auto e = Event::parse(
            {{"id", t->item(r, 0)->data(Qt::UserRole).toString()},
             {"title", t->item(r, 0)->text()},
             {"start", t->item(r, 1)->text()},
             {"end", t->item(r, 2)->text()},
             {"timezone", t->item(r, 3)->text()},
             {"template", static_cast<QComboBox *>(t->cellWidget(r, 4))
                              ->currentData()
                              .toString()},
             {"enabled",
              static_cast<QCheckBox *>(t->cellWidget(r, 6))->isChecked()}});
        out.append(
            QJsonObject{{"event", e.json()}, {"rrule", t->item(r, 5)->text()}});
      }
      send("recurrences.save", {{"items", out}});
      break;
    } catch (const std::exception &e) {
      QMessageBox::warning(this, tr("Error"), QString::fromUtf8(e.what()));
    }
  }
}
void Dock::settings() {
  QDialog d(this);
  d.setWindowTitle(tr("Settings"));
  auto *l = new QVBoxLayout(&d);
  auto *tabs = new QTabWidget;
  l->addWidget(tabs);
  auto page = [&](const char *key) {
    auto *w = new QWidget;
    auto *f = new QFormLayout(w);
    tabs->addTab(w, tr(key));
    return f;
  };
  auto s = current["settings"].toObject();
  auto *f = page("General");
  auto *enabled = check(s["enabled"].toBool(true));
  auto *tz = timezoneBox(s["timezone"].toString("UTC"));
  auto *missed =
      combo({"ignore", "immediate", "ask"}, s["missed"].toString("ignore"));
  auto *tolerance = spin(s["tolerance_seconds"].toInt(60), 1, 86400);
  f->addRow(tr("Enabled"), enabled);
  f->addRow(tr("Timezone"), tz);
  f->addRow(tr("MissedBehavior"), missed);
  f->addRow(tr("ToleranceSeconds"), tolerance);
  f = page("Calendars");
  auto *cal = new QPushButton(tr("Calendars"));
  f->addRow(cal);
  connect(cal, &QPushButton::clicked, this, &Dock::calendars);
  f = page("Google");
  auto g = current["google"].toObject();
  auto *client = new QLineEdit(g["client_id"].toString());
  auto *secret = new QLineEdit;
  secret->setEchoMode(QLineEdit::Password);
  f->addRow(tr("ClientID"), client);
  f->addRow(tr("ClientSecret"), secret);
  auto *googleButtons = new QHBoxLayout;
  f->addRow(googleButtons);
  button(googleButtons, "ConnectGoogle", [&] {
    send("google.save",
         {{"client_id", client->text()}, {"client_secret", secret->text()}});
    send("google.connect");
  });
  button(googleButtons, "Disconnect", [this] { send("google.disconnect"); });
  button(googleButtons, "GoogleCalendars", [this] { send("google.list"); });
  f = page("API");
  auto *apiEnabled = check(s["api_enabled"].toBool());
  auto *host = new QLineEdit(s["api_host"].toString("127.0.0.1"));
  auto *port = spin(s["api_port"].toInt(8766), 1024, 65535);
  f->addRow(tr("Enabled"), apiEnabled);
  f->addRow(tr("Host"), host);
  f->addRow(tr("Port"), port);
  auto *token = new QPushButton(tr("RegenerateToken"));
  f->addRow(token);
  connect(token, &QPushButton::clicked, this,
          [this] { send("token.generate"); });
  f = page("Safety");
  auto *record =
      combo({"exact", "ask", "grace"}, s["record_stop"].toString("exact"));
  auto *stream =
      combo({"exact", "ask", "grace"}, s["stream_stop"].toString("exact"));
  auto *grace = spin(s["grace_minutes"].toInt(15), 0, 1440);
  auto *unowned = check(s["allow_unowned_stop"].toBool());
  auto *existingRecord = combo({"leave", "ignore", "restart"},
                               s["record_existing"].toString("leave"));
  auto *existingStream = combo({"leave", "ignore", "restart"},
                               s["stream_existing"].toString("leave"));
  f->addRow(tr("RecordingStopPolicy"), record);
  f->addRow(tr("StreamingStopPolicy"), stream);
  f->addRow(tr("GraceMinutes"), grace);
  f->addRow(tr("AllowUnowned"), unowned);
  f->addRow(tr("ExistingRecording"), existingRecord);
  f->addRow(tr("ExistingStreaming"), existingStream);
  f = page("Advanced");
  auto *advanced = check(s["advanced_actions"].toBool());
  auto *debug = check(s["debug"].toBool());
  f->addRow(tr("AdvancedActions"), advanced);
  f->addRow(tr("Debug"), debug);
  auto *db = new QLineEdit(current["database"].toString());
  db->setReadOnly(true);
  f->addRow(tr("Database"), db);
  auto *open = new QPushButton(tr("OpenDataFolder"));
  f->addRow(open);
  connect(open, &QPushButton::clicked, this, [this] {
    QDesktopServices::openUrl(QUrl::fromLocalFile(
        QFileInfo(current["database"].toString()).absolutePath()));
  });
  buttons(&d, l);
  d.resize(660, 440);
  if (d.exec() == QDialog::Accepted) {
    bool network = !QHostAddress(host->text()).isLoopback();
    if (apiEnabled->isChecked() && network &&
        QMessageBox::warning(this, tr("API"), tr("NetworkWarning"),
                             QMessageBox::Yes | QMessageBox::No,
                             QMessageBox::No) != QMessageBox::Yes)
      return;
    if (advanced->isChecked() && !s["advanced_actions"].toBool() &&
        QMessageBox::warning(this, tr("Advanced"), tr("AdvancedWarning"),
                             QMessageBox::Yes | QMessageBox::No,
                             QMessageBox::No) != QMessageBox::Yes)
      return;
    send("google.save",
         {{"client_id", client->text()}, {"client_secret", secret->text()}});
    send("settings.save",
         {{"enabled", enabled->isChecked()},
          {"timezone", tz->currentText()},
          {"missed", missed->currentData().toString()},
          {"tolerance_seconds", tolerance->value()},
          {"api_enabled", apiEnabled->isChecked()},
          {"api_host", host->text()},
          {"api_port", port->value()},
          {"api_network_acknowledged", network},
          {"record_stop", record->currentData().toString()},
          {"stream_stop", stream->currentData().toString()},
          {"grace_minutes", grace->value()},
          {"allow_unowned_stop", unowned->isChecked()},
          {"record_existing", existingRecord->currentData().toString()},
          {"stream_existing", existingStream->currentData().toString()},
          {"advanced_actions", advanced->isChecked()},
          {"debug", debug->isChecked()}});
  }
}
void Dock::safetyQuestion(QString key, QString title, QString kind,
                          QString scheduled) {
  auto *d = new QDialog(this);
  d->setAttribute(Qt::WA_DeleteOnClose);
  d->setWindowTitle(tr("ScheduledStop"));
  auto *l = new QVBoxLayout(d);
  auto *text = new QLabel(
      title + "\n" + tr(kind == "stop" ? "StopWarning" : "MissedWarning") +
      "\n" +
      display(scheduled,
              current["settings"].toObject()["timezone"].toString("UTC")));
  text->setTextFormat(Qt::PlainText);
  text->setWordWrap(true);
  l->addWidget(text);
  auto *b = new QHBoxLayout;
  l->addLayout(b);
  button(b, "StopAsScheduled", [this, d, key] {
    send("answer", {{"key", key}, {"minutes", 0}});
    d->accept();
  });
  if (kind == "stop")
    for (int n : {15, 30, 60}) {
      auto *button =
          new QPushButton(tr("Extend") + " " + QString::number(n) + " min");
      b->addWidget(button);
      connect(button, &QPushButton::clicked, d, [this, d, key, n] {
        send("answer", {{"key", key}, {"minutes", n}});
        d->accept();
      });
    }
  button(b, "CancelScheduledAction", [this, d, key] {
    send("answer", {{"key", key}, {"minutes", -1}});
    d->accept();
  });
  connect(d, &QDialog::rejected, this,
          [this, key] { send("answer", {{"key", key}, {"minutes", -1}}); });
  d->show();
}
} // namespace bs
