#include "store.hpp"
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QVariant>
namespace bs {
namespace {
QString compact(const QJsonObject &o) {
  return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}
QJsonObject object(const QJsonValue &v) {
  return QJsonDocument::fromJson(v.toString().toUtf8()).object();
}
struct Statement {
  sqlite3_stmt *s = nullptr;
  sqlite3 *d;
  Statement(sqlite3 *db, const QString &q, const QVariantList &args) : d(db) {
    if (sqlite3_prepare_v2(db, q.toUtf8().constData(), -1, &s, nullptr) !=
        SQLITE_OK)
      throw Error(QString::fromUtf8(sqlite3_errmsg(db)));
    int i = 1;
    for (auto &v : args) {
      if (v.metaType().id() == QMetaType::LongLong ||
          v.metaType().id() == QMetaType::Int ||
          v.metaType().id() == QMetaType::Bool)
        sqlite3_bind_int64(s, i, v.toLongLong());
      else {
        auto b = v.toString().toUtf8();
        sqlite3_bind_text(s, i, b.constData(), b.size(), SQLITE_TRANSIENT);
      }
      ++i;
    }
  }
  ~Statement() { sqlite3_finalize(s); }
  int step() {
    int r = sqlite3_step(s);
    if (r != SQLITE_ROW && r != SQLITE_DONE)
      throw Error(QString::fromUtf8(sqlite3_errmsg(d)));
    return r;
  }
};
} // namespace
Store::Store(const QString &path) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  if (sqlite3_open_v2(path.toUtf8().constData(), &db,
                      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                          SQLITE_OPEN_FULLMUTEX,
                      nullptr) != SQLITE_OK) {
    QString e = QString::fromUtf8(sqlite3_errmsg(db));
    sqlite3_close(db);
    db = nullptr;
    throw Error(e);
  }
  sqlite3_busy_timeout(db, 3000);
  sql("PRAGMA journal_mode=WAL; PRAGMA synchronous=FULL; PRAGMA "
      "foreign_keys=ON;");
  auto v =
      query("PRAGMA user_version").first().toObject()["user_version"].toInt();
  if (v > 2)
    throw Error(
        "Database was created by a newer plugin; refusing to modify it");
  if (v == 0)
    sql("BEGIN IMMEDIATE; CREATE TABLE events(id TEXT PRIMARY KEY, calendar "
        "TEXT NOT NULL, external_id TEXT NOT NULL, data TEXT NOT NULL); CREATE "
        "UNIQUE INDEX external_identity ON events(calendar,external_id) WHERE "
        "external_id<>''; CREATE TABLE templates(id TEXT PRIMARY KEY,data TEXT "
        "NOT NULL); CREATE TABLE config(key TEXT PRIMARY KEY,data TEXT NOT "
        "NULL); CREATE TABLE executions(key TEXT PRIMARY KEY,event_id TEXT NOT "
        "NULL,title TEXT NOT NULL,action TEXT NOT NULL,scheduled INTEGER NOT "
        "NULL,actual INTEGER,result TEXT NOT NULL,message TEXT NOT NULL); "
        "CREATE TABLE deferred(key TEXT PRIMARY KEY,due INTEGER NOT NULL,state "
        "TEXT NOT NULL); CREATE TABLE logs(id INTEGER PRIMARY KEY,time INTEGER "
        "NOT NULL,level TEXT NOT NULL,message TEXT NOT NULL); CREATE TABLE "
        "ignored_events(calendar TEXT NOT NULL,external_id TEXT NOT NULL,"
        "created INTEGER NOT NULL,PRIMARY KEY(calendar,external_id)); PRAGMA "
        "user_version=2; COMMIT;");
  if (v == 1)
    sql("BEGIN IMMEDIATE; CREATE TABLE ignored_events(calendar TEXT NOT NULL,"
        "external_id TEXT NOT NULL,created INTEGER NOT NULL,PRIMARY KEY("
        "calendar,external_id)); PRAGMA user_version=2; COMMIT;");
  run("UPDATE executions SET result='indeterminate',message='OBS exited after "
      "durable claim; not replayed automatically' WHERE result IN ('claimed','requested')");
}
Store::~Store() {
  if (db)
    sqlite3_close(db);
}
void Store::sql(const QString &q) {
  char *e = nullptr;
  if (sqlite3_exec(db, q.toUtf8().constData(), nullptr, nullptr, &e) !=
      SQLITE_OK) {
    QString s = QString::fromUtf8(e);
    sqlite3_free(e);
    throw Error(s);
  }
}
void Store::run(const QString &q, const QVariantList &a) {
  Statement s(db, q, a);
  s.step();
}
QJsonArray Store::query(const QString &q, const QVariantList &a) const {
  Statement s(db, q, a);
  QJsonArray rows;
  while (s.step() == SQLITE_ROW) {
    QJsonObject o;
    for (int i = 0; i < sqlite3_column_count(s.s); ++i) {
      auto name = QString::fromUtf8(sqlite3_column_name(s.s, i));
      if (sqlite3_column_type(s.s, i) == SQLITE_INTEGER)
        o[name] = double(sqlite3_column_int64(s.s, i));
      else
        o[name] = QString::fromUtf8(
            reinterpret_cast<const char *>(sqlite3_column_text(s.s, i)));
    }
    rows.append(o);
  }
  return rows;
}
QList<Event> Store::events() const {
  QList<Event> r;
  for (auto v : query("SELECT data FROM events ORDER BY id"))
    r.append(Event::parse(object(v.toObject()["data"])));
  return r;
}
QList<Template> Store::templates() const {
  QList<Template> r;
  for (auto v : query("SELECT data FROM templates ORDER BY id"))
    r.append(Template::parse(object(v.toObject()["data"])));
  return r;
}
Event Store::event(const QString &id) const {
  auto r = query("SELECT data FROM events WHERE id=?", {id});
  if (r.isEmpty())
    throw Error("Event not found");
  return Event::parse(object(r.first().toObject()["data"]));
}
void Store::put(Event e) {
  e.validate();
  if (!e.externalId.isEmpty()) {
    auto rows =
        query("SELECT id FROM events WHERE calendar=? AND external_id=?",
              {e.calendar, e.externalId});
    if (!rows.isEmpty())
      e.id = rows.first().toObject()["id"].toString();
  }
  auto rows = query("SELECT data FROM events WHERE id=?", {e.id});
  if (!rows.isEmpty()) {
    auto old = Event::parse(object(rows.first().toObject()["data"]));
    if (old.start != e.start || old.end != e.end ||
        old.templateId != e.templateId || old.enabled != e.enabled)
      run("DELETE FROM deferred WHERE substr(key,1,?)=?", {e.id.size()+1, e.id + "/"});
  }
  run("INSERT INTO events VALUES(?,?,?,?) ON CONFLICT(id) DO UPDATE SET "
      "calendar=excluded.calendar,external_id=excluded.external_id,data="
      "excluded.data",
      {e.id, e.calendar, e.externalId, compact(e.json())});
  ++generation;
}
void Store::put(const Template &t) {
  run("INSERT INTO templates VALUES(?,?) ON CONFLICT(id) DO UPDATE SET "
      "data=excluded.data",
      {t.id, compact(t.json())});
  run("DELETE FROM deferred WHERE key IN (SELECT d.key FROM deferred d JOIN "
      "events e ON substr(d.key,1,length(e.id)+1)=e.id||'/' WHERE "
      "json_extract(e.data,'$.template')=?)",
      {t.id});
  ++generation;
}
void Store::erase(const QString &id) {
  run("DELETE FROM events WHERE id=?", {id});
  run("DELETE FROM deferred WHERE substr(key,1,?)=?",
      {id.size() + 1, id + "/"});
  ++generation;
}
void Store::ignoreExternal(const Event &e) {
  if (e.calendar.isEmpty() || e.externalId.isEmpty())
    throw Error("Event has no external provider identity");
  sql("BEGIN IMMEDIATE");
  try {
    run("INSERT OR REPLACE INTO ignored_events VALUES(?,?,?)",
        {e.calendar, e.externalId, now()});
    run("DELETE FROM events WHERE id=?", {e.id});
    run("DELETE FROM deferred WHERE substr(key,1,?)=?",
        {e.id.size() + 1, e.id + "/"});
    sql("COMMIT");
    ++generation;
  } catch (...) {
    sql("ROLLBACK");
    throw;
  }
}
void Store::clearIgnored(const QString &calendar) {
  run("DELETE FROM ignored_events WHERE calendar=?", {calendar});
}
void Store::replaceCalendar(const QString &id, const QList<Event> &list) {
  sql("BEGIN IMMEDIATE");
  try {
    QSet<QString> keep;
    QSet<QString> ignored;
    for (auto v : query("SELECT external_id FROM ignored_events WHERE calendar=?",
                        {id}))
      ignored.insert(v.toObject()["external_id"].toString());
    for (auto e : list) {
      if (e.calendar != id || e.externalId.isEmpty())
        throw Error("Invalid provider identity");
      if (ignored.contains(e.externalId))
        continue;
      put(e);
      keep.insert(e.externalId);
    }
    for (auto v :
         query("SELECT id,external_id FROM events WHERE calendar=?", {id})) {
      auto o = v.toObject();
      if (!keep.contains(o["external_id"].toString()))
        erase(o["id"].toString());
    }
    sql("COMMIT");
  } catch (...) {
    sql("ROLLBACK");
    throw;
  }
}
QJsonObject Store::config(const QString &k) const {
  auto r = query("SELECT data FROM config WHERE key=?", {k});
  return r.isEmpty() ? QJsonObject{} : object(r.first().toObject()["data"]);
}
void Store::config(const QString &k, const QJsonObject &v) {
  run("INSERT INTO config VALUES(?,?) ON CONFLICT(key) DO UPDATE SET "
      "data=excluded.data",
      {k, compact(v)});
}
bool Store::claim(const Due &d) {
  sql("BEGIN IMMEDIATE");
  try {
    run("INSERT OR IGNORE INTO executions VALUES(?,?,?,?,?,?,?,?)",
        {d.key(), d.event.id, d.event.title, d.action.type, d.time, now(), "claimed", ""});
    const bool inserted = sqlite3_changes(db) == 1;
    if (inserted && d.action.type == "record.start")
      config("recording/" + d.event.id, d.event.json());
    sql("COMMIT");
    return inserted;
  } catch (...) {
    sql("ROLLBACK");
    throw;
  }
}
bool Store::finished(const QString &k) const {
  return !query("SELECT key FROM executions WHERE key=?", {k}).isEmpty();
}
void Store::finish(const QString &k, const QString &r, const QString &m) {
  run("UPDATE executions SET actual=?,result=?,message=? WHERE key=?",
      {now(), r, m, k});
  run("DELETE FROM deferred WHERE key=?", {k});
  if (k.endsWith("/record.stop"))
    run("DELETE FROM config WHERE key=?", {"recording/" + k.left(k.size() - QString("/record.stop").size())});
}
void Store::defer(const Due &d, qint64 until, const QString &state) {
  run("INSERT INTO deferred VALUES(?,?,?) ON CONFLICT(key) DO UPDATE SET "
      "due=excluded.due,state=excluded.state",
      {d.key(), until, state});
}
QJsonArray Store::history() const {
  return query("SELECT * FROM executions ORDER BY actual DESC LIMIT 1000");
}
void Store::log(const QString &l, const QString &m) {
  run("INSERT INTO logs(time,level,message) VALUES(?,?,?)", {now(), l, m});
}
} // namespace bs
