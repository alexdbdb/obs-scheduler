#pragma once
#include "runtime.hpp"
#include <QCalendarWidget>
#include <QComboBox>
#include <QLabel>
#include <QTableWidget>
#include <QWidget>
namespace bs {
QString tr(const char *key);
class Dock : public QWidget {
  Q_OBJECT
  Runtime *runtime;
  QJsonObject current;
  QLabel *dashboard;
  QTableWidget *agenda, *calendarEvents, *history, *logs;
  QCalendarWidget *calendar;
  QComboBox *view;
  void send(QString op, QJsonObject data = {});
  void renderCalendar();
  void eventDialog(QJsonObject event = {}, bool duplicate = false);
  void calendars();
  void recurrences();
  void settings();

public:
  explicit Dock(Runtime *r, QWidget *parent = nullptr);
public slots:
  void updateState(QJsonObject state);
};
} // namespace bs
