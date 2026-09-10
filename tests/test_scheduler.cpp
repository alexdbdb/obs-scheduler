#include "calendar.hpp"
#include "scheduler.hpp"
#include "providers.hpp"
#include "api.hpp"
#include "messages.hpp"
#include <QCryptographicHash>
#include <QTcpSocket>
#include <QUrlQuery>
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
private slots:
  void localApiPolicy() {
    Api api;
    const auto hash = QString::fromLatin1(
        QCryptographicHash::hash("test-only", QCryptographicHash::Sha256).toHex());
    QJsonObject settings{{"api_enabled", true}, {"api_token_hash", hash},
                         {"api_host", "0.0.0.0"}, {"api_port", 18766},
                         {"api_network_acknowledged", true}};
    QVERIFY_EXCEPTION_THROWN(api.configure(settings), Error);
    QVERIFY(!api.running());
    settings["api_host"] = "192.0.2.1";
    QVERIFY_EXCEPTION_THROWN(api.configure(settings), Error);
    settings["api_host"] = "::";
    QVERIFY_EXCEPTION_THROWN(api.configure(settings), Error);
  }
  void diagnosticTranslation() {
    MessageCatalog catalog(QJsonArray{
        QJsonObject{{"key", "failed"}, {"source", "Calendar %1 failed: %2"}},
        QJsonObject{{"key", "denied"}, {"source", "Access denied"}}});
    auto spanish = [](const QString &key) {
      return key == "failed" ? QString("Calendario %1 fallido: %2")
           : key == "denied" ? QString("Acceso denegado") : key;
    };
    QCOMPARE(catalog.translate("Calendar work failed: Access denied", spanish),
             QString("Calendario work fallido: Acceso denegado"));
    QCOMPARE(catalog.translate("Calendar %2 failed: Access denied", spanish),
             QString("Calendario %2 fallido: Acceso denegado"));
    QCOMPARE(catalog.translate("Unrecognized external detail", spanish),
             QString("Unrecognized external detail"));
    QCOMPARE(catalog.translate("Calendar work failed: Access denied",
                              [](const QString &key) { return key; }),
             QString("Calendar work failed: Access denied"));
  }
  void formUrlEncoding() {
    QCOMPARE(formUrlEncoded({{"plain", "safe-._~"},
                             {"space", "a b"},
                             {"plus", "a+b"},
                             {"reserved", "a&b=c%2F"},
                             {"unicode", QString::fromUtf8("España")}}),
             QByteArray("plain=safe-._~&space=a%20b&plus=a%2Bb&"
                        "reserved=a%26b%3Dc%252F&unicode=Espa%C3%B1a"));
    QCOMPARE(googleApiError(
                 R"({"error":{"code":403,"message":"Calendar API has not been used in project 123 before or it is disabled.","errors":[{"reason":"accessNotConfigured"}]}})",
                 403),
             QString("Calendar HTTP request failed (HTTP 403; "
                     "accessNotConfigured; Calendar API has not been used in "
                     "project 123 before or it is disabled.)"));
  }

  void googleBrowserAuthorization() {
#ifndef Q_OS_WIN
    QSKIP("Uses Windows DPAPI in an isolated temporary directory");
#else
    QTemporaryDir d;
    Store s(d.path() + "/db");
    Secrets secrets(d.path());
    Providers p(s, secrets);
    QVERIFY_EXCEPTION_THROWN(p.configureGoogle("not-a-client-id"), Error);
    p.configureGoogle("test.apps.googleusercontent.com", "GOCSPX-test-secret");
    QCOMPARE(p.googleStatus()["client_id"].toString(),
             QString("test.apps.googleusercontent.com"));
    QVERIFY(p.googleStatus()["client_secret_configured"].toBool());
    QVERIFY(!p.googleStatus().contains("client_secret"));
    QCOMPARE(secrets.read("google-client-secret"),
             QByteArray("GOCSPX-test-secret"));
    p.configureGoogle("test.apps.googleusercontent.com");
    QCOMPARE(secrets.read("google-client-secret"),
             QByteArray("GOCSPX-test-secret"));
    QSignalSpy opened(&p, &Providers::openUrl);
    QSignalSpy errors(&p, &Providers::problem);
    p.connectGoogle();
    QCOMPARE(opened.size(), 1);
    QUrlQuery auth(QUrl(opened.first().first().toString()));
    QCOMPARE(auth.queryItemValue("prompt"), QString("select_account consent"));
    QCOMPARE(auth.queryItemValue("code_challenge_method"), QString("S256"));
    QCOMPARE(auth.queryItemValue("code_challenge").size(), 43);
    QVERIFY(auth.queryItemValue("scope").contains("calendar.events.readonly"));
    QVERIFY(auth.queryItemValue("scope").contains("calendar.calendarlist.readonly"));
    QVERIFY(!auth.hasQueryItem("client_secret"));
    QVERIFY(p.googleStatus()["connecting"].toBool());
    QVERIFY(p.googleStatus()["configured"].toBool());
    p.connectGoogle();
    QCOMPARE(opened.size(), 1); // Double-click must not create another attempt.
    const QUrl redirect(auth.queryItemValue("redirect_uri"));
    QCOMPARE(redirect.host(), QString("127.0.0.1"));
    QTcpSocket wrong;
    wrong.connectToHost(redirect.host(), quint16(redirect.port()));
    QVERIFY(wrong.waitForConnected());
    wrong.write("GET /oauth/callback?state=wrong&error=access_denied HTTP/1.1\r\nHost: localhost\r\n\r\n");
    QTRY_VERIFY(wrong.bytesAvailable() > 0);
    QVERIFY(wrong.readAll().startsWith("HTTP/1.1 400"));
    QVERIFY(p.googleStatus()["connecting"].toBool());
    QTcpSocket denied;
    denied.connectToHost(redirect.host(), quint16(redirect.port()));
    QVERIFY(denied.waitForConnected());
    denied.write("GET /oauth/callback?state=" + auth.queryItemValue("state").toUtf8() +
                 "&error=access_denied HTTP/1.1\r\nHost: localhost\r\n\r\n");
    QTRY_COMPARE(errors.size(), 1);
    QVERIFY(!p.googleStatus()["connecting"].toBool());
    QVERIFY(!p.googleStatus()["connected"].toBool());
    p.connectGoogle();
    QCOMPARE(opened.size(), 2);
    QUrlQuery retry(QUrl(opened.last().first().toString()));
    QVERIFY(retry.queryItemValue("state") != auth.queryItemValue("state"));
    p.disconnectGoogle();
    QVERIFY(!p.googleStatus()["connecting"].toBool());
#endif
  }
  void odooConfiguration() {
#ifndef Q_OS_WIN
    QSKIP("Uses Windows DPAPI in an isolated temporary directory");
#else
    QTemporaryDir directory;
    Store store(directory.path() + "/db");
    Secrets secrets(directory.path());
    Providers providers(store, secrets);
    QVERIFY_EXCEPTION_THROWN(
        providers.configureOdoo({{"url", "http://odoo.example.com"},
                                 {"database", "demo"},
                                 {"login", "user@example.com"}},
                                "secret"),
        Error);
    const QJsonObject configuration{
        {"url", "https://odoo.example.com"},
        {"database", "demo"},
        {"login", "user@example.com"},
        {"protocol", "jsonrpc"},
        {"enabled", true},
        {"refresh_minutes", 20}};
    providers.configureOdoo(configuration, "test-api-key");
    const auto status = providers.odooStatus();
    QVERIFY(status["configured"].toBool());
    QVERIFY(status["api_key_configured"].toBool());
    QVERIFY(!status.contains("api_key"));
    QVERIFY(!store.config("odoo").contains("api_key"));
    QCOMPARE(secrets.read("odoo-api-key"), QByteArray("test-api-key"));
    QCOMPARE(store.config("calendars")["items"].toArray().size(), 1);
    QCOMPARE(store.config("calendars")["items"].toArray().first().toObject()["id"],
             QJsonValue("odoo-events"));
    providers.configureOdoo(configuration);
    QCOMPARE(secrets.read("odoo-api-key"), QByteArray("test-api-key"));
    providers.disconnectOdoo();
    QVERIFY(!providers.odooStatus()["configured"].toBool());
    QVERIFY(secrets.read("odoo-api-key").isEmpty());
    QVERIFY(store.config("calendars")["items"].toArray().isEmpty());
#endif
  }
  void initTestCase() { CalendarParser::setZoneDirectory(qEnvironmentVariable("BS_ZONEINFO")); }
  void offsets() {
    auto e = sample();
    auto p = plan({e});
    QCOMPARE(p.size(), 2);
    QCOMPARE(p[0].time, e.start);
    QCOMPARE(p[1].time, e.end);
  }
  void serialization() {
    auto e = sample();
    auto round = Event::parse(e.json());
    QCOMPARE(round.start, e.start);
    QCOMPARE(round.timezone, e.timezone);
    e.source = "OdooEvent";
    e.calendar = "odoo-events";
    e.externalId = "event.event/42";
    e.validate();
    QCOMPARE(Event::parse(e.json()).source, QString("OdooEvent"));
    for (const auto value : {0LL, 1789066800123LL, now()})
      QCOMPARE(instant(iso(value)).toMSecsSinceEpoch(), value);
    QVERIFY_EXCEPTION_THROWN(instant("2026-01-01T12:00:00"), Error);
    QCOMPARE(instant("2026-09-10T21:00:00+02:00"),
             instant("2026-09-10T19:00:00Z"));
    QCOMPARE(instant("2026-09-10T19:00:00+0200"),
             instant("2026-09-10T17:00:00Z"));
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
      s.config("settings", {{"missed", "immediate"}});
      Scheduler sch(s);
      sch.execute = [&](auto &) {
        ++count;
        return Outcome{"succeeded", ""};
      };
      sch.tick(e.start);
      QCOMPARE(count, 1);
    }
    {
      Store s(dir.path() + "/db");
      Scheduler sch(s);
      sch.execute = [&](auto &) {
        ++count;
        return Outcome{"succeeded", ""};
      };
      sch.tick(e.start);
      QCOMPARE(count, 1);
      QCOMPARE(s.history().size(), 1);
    }
  }
  void indeterminate() {
    QTemporaryDir d;
    auto e = sample();
    auto due = plan({e}).first();
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
    Scheduler sch(s);
    int n = 0;
    sch.execute = [&](auto &) {
      ++n;
      return Outcome{"succeeded", ""};
    };
    sch.tick(e.start);
    QCOMPARE(n, 1);
    QCOMPARE(s.history().size(), 1);
  }
  void recordingSchedule() {
    QTemporaryDir d;
    Store s(d.path() + "/db");
    auto e = sample(now());
    s.put(e);
    Scheduler sch(s);
    int n = 0;
    sch.execute = [&](auto &) {
      ++n;
      return Outcome{"succeeded", ""};
    };
    sch.tick(e.start);
    sch.tick(e.end);
    QCOMPARE(n, 2);
  }
  void deletedAndModified() {
    QTemporaryDir d;
    Store s(d.path() + "/db");
    auto e = sample(now() + 3600000);
    s.put(e);
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
    QCOMPARE(n, 0);
    sch.tick(e.start);
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
    Scheduler sch(s);
    int n = 0;
    sch.execute = [&](auto &) {
      ++n;
      return Outcome{"succeeded", ""};
    };
    sch.tick(e.start);
    QCOMPARE(n, 2);
    sch.tick(e.start - 3600000);
    sch.tick(e.start);
    QCOMPARE(n, 2);
  }
  void activeRecordingKeepsStopAfterDeletionAndPause() {
    QTemporaryDir d;
    Store s(d.path() + "/db");
    auto e = sample(now());
    s.put(e);
    Scheduler sch(s);
    QStringList actions;
    sch.execute = [&](const Due &due) {
      actions.append(due.action.type);
      return Outcome{"succeeded", ""};
    };
    sch.tick(e.start);
    s.erase(e.id);
    s.config("settings", {{"enabled", false}});
    sch.tick(e.end);
    QCOMPARE(actions, QStringList({"record.start", "record.stop"}));
    QVERIFY(s.config("recording/" + e.id).isEmpty());
  }
  void missedStartIsNotDispatched() {
    QTemporaryDir d;
    Store s(d.path() + "/db");
    auto e = sample(now());
    s.put(e);
    Scheduler sch(s);
    int dispatched = 0;
    sch.execute = [&](const Due &) {
      ++dispatched;
      return Outcome{"succeeded", ""};
    };
    sch.tick(e.start + 60001);
    QCOMPARE(dispatched, 0);
    QCOMPARE(s.history().first().toObject()["result"].toString(), QString("skipped"));
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
  void recordingFilenameUsesEventDateAndSafeTitle() {
    auto event = sample();
    event.start = QDateTime(QDate(2026, 9, 10), QTime(12, 0),
                            QTimeZone::systemTimeZone())
                      .toMSecsSinceEpoch();
    event.title = "  Gala: 50% / Final?  ";
    QCOMPARE(recordingFilename(event),
             QString("2026-09-10 - Gala_ 50_ _ Final_"));
    event.title = "<>:\"/\\|?*%";
    QCOMPARE(recordingFilename(event), QString("2026-09-10 - _"));
  }
  void restoreExcludedIsScopedAndSurvivesRestart() {
    QTemporaryDir d;
    const auto path = d.path() + "/db";
    auto a = sample();
    a.calendar = "odoo-events";
    a.externalId = "event.event/42";
    a.source = "OdooEvent";
    auto b = a;
    b.id = "second";
    b.externalId = "event.event/43";
    {
      Store s(path);
      s.replaceCalendar(a.calendar, {a, b});
      s.ignoreExternal(a);
      s.ignoreExternal(b);
      s.restoreExternal("other-calendar", a.externalId);
      QCOMPARE(s.query("SELECT * FROM ignored_events").size(), 2);
      s.restoreExternal(a.calendar, a.externalId);
      s.restoreExternal(a.calendar, a.externalId);
      QCOMPARE(s.query("SELECT * FROM ignored_events").size(), 1);
    }
    Store s(path);
    s.replaceCalendar(a.calendar, {a, b});
    QCOMPARE(s.events().size(), 1);
    QCOMPARE(s.events().first().externalId, a.externalId);
    s.restoreExternal(b.calendar, b.externalId);
    s.replaceCalendar(a.calendar, {a, b});
    QCOMPARE(s.events().size(), 2);
  }
  void ignoredExternalEventPersists() {
    QTemporaryDir d;
    const auto path = d.path() + "/db";
    auto e = sample();
    e.calendar = "google-calendar";
    e.externalId = "google-event";
    e.source = "GoogleCalendar";
    {
      Store s(path);
      s.replaceCalendar(e.calendar, {e});
      QCOMPARE(s.events().size(), 1);
      s.ignoreExternal(s.events().first());
      QCOMPARE(s.events().size(), 0);
      s.replaceCalendar(e.calendar, {e});
      QCOMPARE(s.events().size(), 0);
    }
    {
      Store s(path);
      QCOMPARE(s.query("PRAGMA user_version").first().toObject()["user_version"].toInt(), 2);
      s.replaceCalendar(e.calendar, {e});
      QCOMPARE(s.events().size(), 0);
      s.clearIgnored(e.calendar);
      s.replaceCalendar(e.calendar, {e});
      QCOMPARE(s.events().size(), 1);
    }
  }
};
QTEST_GUILESS_MAIN(Tests)
#include "test_scheduler.moc"
