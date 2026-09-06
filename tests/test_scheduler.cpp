#include "calendar.hpp"
#include "scheduler.hpp"
#include <QTemporaryDir>
#include <QtTest>
using namespace bs;
class Tests : public QObject {
  Q_OBJECT
  Event sample(qint64 start = 1789066800000LL) {
    return Event::parse({{"id", "event"},
                         {"title", "Artist X"},
                         {"start", iso(start)},
                         {"end", iso(start + 7200000)},
                         {"template", "concert"},
                         {"timezone", "Europe/Madrid"}});
  }
  Template concert() {
    return Template{
        "concert",
        "Concert",
        {{"scene", "scene.current", "start", -600, {{"scene", "GENERAL"}}},
         {"record", "record.start", "start", -300, {}},
         {"stop", "record.stop", "end", 900, {}}}};
  }
private slots:
  void initTestCase() { CalendarParser::setZoneDirectory(qEnvironmentVariable("BS_ZONEINFO")); }
  void offsets() {
    auto e = sample();
    auto p = plan({e}, {concert()});
    QCOMPARE(p.size(), 3);
    QCOMPARE(p[0].time, e.start - 600000);
    QCOMPARE(p[1].time, e.start - 300000);
    QCOMPARE(p[2].time, e.end + 900000);
  }
  void serialization() {
    auto e = sample();
    auto round = Event::parse(e.json());
    QCOMPARE(round.start, e.start);
    QCOMPARE(round.timezone, e.timezone);
    QVERIFY_EXCEPTION_THROWN(instant("2026-01-01T12:00:00"), Error);
    QCOMPARE(instant("2026-09-10T21:00:00+02:00"),
             instant("2026-09-10T19:00:00Z"));
  }
  void overnight() {
    auto e = Event::parse({{"title", "Night"},
                           {"start", "2026-09-10T23:00:00+02:00"},
                           {"end", "2026-09-11T02:00:00+02:00"}});
    QCOMPARE(e.end - e.start, 10800000);
  }
  void dedupUpdates() {
    QTemporaryDir dir;
    Store s(dir.path() + "/db");
    auto e = sample();
    e.calendar = "ics";
    e.externalId = "UID/instance";
    e.source = "ICS";
    s.put(e);
    e.id = "new-id";
    e.title = "Updated";
    s.put(e);
    QCOMPARE(s.events().size(), 1);
    QCOMPARE(s.events().first().id, QString("event"));
    QCOMPARE(s.events().first().title, QString("Updated"));
  }
  void schedulerRestart() {
    QTemporaryDir dir;
    int count = 0;
    auto e = sample(now());
    {
      Store s(dir.path() + "/db");
      s.put(e);
      s.put(concert());
      s.config("settings", {{"missed", "immediate"}});
      Scheduler sch(s);
      sch.execute = [&](auto &) {
        ++count;
        return Outcome{"succeeded", ""};
      };
      sch.tick(e.start);
      QCOMPARE(count, 2);
    }
    {
      Store s(dir.path() + "/db");
      Scheduler sch(s);
      sch.execute = [&](auto &) {
        ++count;
        return Outcome{"succeeded", ""};
      };
      sch.tick(e.start);
      QCOMPARE(count, 2);
      QCOMPARE(s.history().size(), 2);
    }
  }
  void indeterminate() {
    QTemporaryDir d;
    auto e = sample();
    auto due = plan({e}, {concert()}).first();
    {
      Store s(d.path() + "/db");
      QVERIFY(s.claim(due));
    }
    {
      Store s(d.path() + "/db");
      QVERIFY(s.finished(due.key()));
      QCOMPARE(s.history().first().toObject()["result"].toString(),
               QString("indeterminate"));
      QVERIFY(!s.claim(due));
    }
  }
  void missed() {
    QTemporaryDir d;
    Store s(d.path() + "/db");
    auto e = sample(now());
    s.put(e);
    s.put(concert());
    Scheduler sch(s);
    int n = 0;
    sch.execute = [&](auto &) {
      ++n;
      return Outcome{"succeeded", ""};
    };
    sch.tick(e.start);
    QCOMPARE(n, 0);
    QCOMPARE(s.history().size(), 2);
  }
  void askAndGrace() {
    QTemporaryDir d;
    Store s(d.path() + "/db");
    auto e = sample(now());
    s.put(e);
    s.put(Template{"concert", "stop", {{"stop", "record.stop", "end", 0, {}}}});
    s.config("settings", {{"record_stop", "ask"}});
    Scheduler sch(s);
    int asked = 0, n = 0;
    QString key;
    sch.ask = [&](const Due &v, const QString &) {
      ++asked;
      key = v.key();
    };
    sch.execute = [&](auto &) {
      ++n;
      return Outcome{"succeeded", ""};
    };
    sch.tick(e.end - 299000);
    sch.tick(e.end);
    QCOMPARE(asked, 1);
    QCOMPARE(n, 0);
    sch.answer(key, 15);
    sch.tick(e.end + 899000);
    QCOMPARE(n, 0);
    sch.tick(e.end + 900000);
    QCOMPARE(n, 1);
  }
  void deletedAndModified() {
    QTemporaryDir d;
    Store s(d.path() + "/db");
    auto e = sample(now() + 3600000);
    s.put(e);
    s.put(concert());
    Scheduler sch(s);
    int n = 0;
    sch.execute = [&](auto &) {
      ++n;
      return Outcome{"succeeded", ""};
    };
    s.erase(e.id);
    sch.tick(e.start);
    QCOMPARE(n, 0);
    s.put(e);
    e.start += 3600000;
    e.end += 3600000;
    s.put(e);
    sch.tick(e.start - 3600000);
    QCOMPARE(n, 0);
    sch.tick(e.start - 300000);
    QCOMPARE(n, 1);
  }
  void simultaneousAndClock() {
    QTemporaryDir d;
    Store s(d.path() + "/db");
    auto e = sample(now());
    auto e2 = e;
    e2.id = "second";
    s.put(e);
    s.put(e2);
    s.put(Template{"concert",
                   "simultaneous",
                   {{"a", "record.start", "start", 0, {}},
                    {"b", "stream.start", "start", 0, {}}}});
    Scheduler sch(s);
    int n = 0;
    sch.execute = [&](auto &) {
      ++n;
      return Outcome{"succeeded", ""};
    };
    sch.tick(e.start);
    QCOMPARE(n, 4);
    sch.tick(e.start - 3600000);
    sch.tick(e.start);
    QCOMPARE(n, 4);
  }
  void ownership() {
    QVERIFY(!stopAllowed(true, false, false));
    QVERIFY(stopAllowed(true, true, false));
    QVERIFY(stopAllowed(true, false, true));
    QVERIFY(!stopAllowed(false, true, true));
  }
  void weeklyDST() {
    auto e = sample();
    e.start = instant("2026-03-22T08:00:00+01:00").toMSecsSinceEpoch();
    e.end = e.start + 7200000;
    auto events = CalendarParser::recurrence(
        e, "FREQ=WEEKLY;COUNT=3", e.start - 1000, e.start + 30LL * 86400000);
    QCOMPARE(events.size(), 3);
    for (auto &v : events)
      QCOMPARE(
          QDateTime::fromMSecsSinceEpoch(v.start, QTimeZone("Europe/Madrid"))
              .time(),
          QTime(8, 0));
    QCOMPARE(events[1].start - events[0].start, 7LL * 86400000 - 3600000);
  }
  void monthly() {
    auto e = sample();
    e.start = instant("2026-09-05T08:00:00+02:00").toMSecsSinceEpoch();
    e.end = e.start + 7200000;
    auto events =
        CalendarParser::recurrence(e, "FREQ=MONTHLY;BYDAY=1SA;COUNT=3",
                                   e.start - 1000, e.start + 100LL * 86400000);
    QCOMPARE(events.size(), 3);
    for (auto &v : events) {
      auto date =
          QDateTime::fromMSecsSinceEpoch(v.start, QTimeZone("Europe/Madrid"))
              .date();
      QCOMPARE(date.dayOfWeek(), 6);
      QVERIFY(date.day() <= 7);
    }
  }
  void ics() {
    QByteArray data =
        "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nBEGIN:VEVENT\r\nUID:"
        "concert\r\nDTSTART;TZID=Europe/"
        "Madrid:20260910T230000\r\nDTEND;TZID=Europe/"
        "Madrid:20260911T020000\r\nSUMMARY:Night concert\r\nDESCRIPTION:Line "
        "one\\nLine two\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";
    auto e = CalendarParser::ics(
        data, {{"id", "feed"}, {"template", "concert"}},
        instant("2026-09-01T00:00:00Z").toMSecsSinceEpoch(),
        instant("2026-10-01T00:00:00Z").toMSecsSinceEpoch());
    QCOMPARE(e.size(), 1);
    QCOMPARE(e.first().end - e.first().start, 10800000);
    QVERIFY(e.first().description.contains('\n'));
    QCOMPARE(e.first().start,
             instant("2026-09-10T21:00:00Z").toMSecsSinceEpoch());
  }
  void allDay() {
    auto events = CalendarParser::ics(
        "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nBEGIN:VEVENT\r\nUID:day\r\nDTSTART;"
        "VALUE=DATE:20260910\r\nSUMMARY:Day\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n",
        {{"id", "feed"}, {"timezone", "Europe/Madrid"}},
        instant("2026-09-01T00:00:00Z").toMSecsSinceEpoch(),
        instant("2026-10-01T00:00:00Z").toMSecsSinceEpoch());
    QCOMPARE(events.size(), 1);
    QVERIFY(events.first().metadata["all_day"].toBool());
    QCOMPARE(events.first().end - events.first().start, 86400000);
  }
  void calendarAtomicDelete() {
    QTemporaryDir d;
    Store s(d.path() + "/db");
    auto e = sample();
    e.calendar = "feed";
    e.externalId = "1";
    s.replaceCalendar("feed", {e});
    auto bad = e;
    bad.externalId = "";
    QVERIFY_EXCEPTION_THROWN(s.replaceCalendar("feed", {bad}), Error);
    QCOMPARE(s.events().size(), 1);
    s.replaceCalendar("feed", {});
    QCOMPARE(s.events().size(), 0);
  }
};
QTEST_GUILESS_MAIN(Tests)
#include "test_scheduler.moc"
