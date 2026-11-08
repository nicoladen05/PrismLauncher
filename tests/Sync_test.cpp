#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include <sync/SyncEngine.h>
#include <sync/SyncGuard.h>
#include <sync/SyncLink.h>
#include <sync/SyncProfile.h>

namespace {
void writeFile(const QString& path, const QByteArray& content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write(content);
}

QByteArray readFile(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    return f.readAll();
}
}  // namespace

class SyncTest : public QObject {
    Q_OBJECT

    QTemporaryDir m_tmp;

    QString path(const QString& relative) const { return QDir(m_tmp.path()).filePath(relative); }

    SyncProfile makeProfile(const QString& name)
    {
        SyncProfile profile(name, path("sync/" + name));
        profile.addItem({ "saves/World", SyncItem::Kind::Directory, false });
        profile.addItem({ "config", SyncItem::Kind::Directory, false });
        profile.addItem({ "options.txt", SyncItem::Kind::File, false });
        return profile;
    }

   private slots:
    void init() { QVERIFY(m_tmp.isValid()); }

    void test_normalizeItemPath()
    {
        QCOMPARE(SyncProfile::normalizeItemPath("saves/World/"), QString("saves/World"));
        QCOMPARE(SyncProfile::normalizeItemPath(" config "), QString("config"));
        QCOMPARE(SyncProfile::normalizeItemPath("saves\\World"), QDir::fromNativeSeparators("saves\\World"));
        QCOMPARE(SyncProfile::normalizeItemPath("config/../options.txt"), QString("options.txt"));
        QVERIFY(SyncProfile::normalizeItemPath("../outside").isEmpty());
        QVERIFY(SyncProfile::normalizeItemPath("/etc/passwd").isEmpty());
        QVERIFY(SyncProfile::normalizeItemPath("C:/Windows").isEmpty());
        QVERIFY(SyncProfile::normalizeItemPath(".").isEmpty());
        QVERIFY(SyncProfile::normalizeItemPath("").isEmpty());
    }

    void test_profileRoundTrip()
    {
        auto profile = makeProfile("roundtrip");
        profile.setName("My worlds");
        profile.addItem({ "config", SyncItem::Kind::Directory, true });  // replaces the existing item
        QVERIFY(!profile.addItem({ "../nope", SyncItem::Kind::File, false }));
        QString error;
        QVERIFY2(profile.save(&error), qPrintable(error));
        QVERIFY(QFileInfo(profile.contentPath()).isDir());

        SyncProfile loaded;
        QVERIFY2(SyncProfile::load(profile.rootPath(), loaded, &error), qPrintable(error));
        QCOMPARE(loaded.id(), QString("roundtrip"));
        QCOMPARE(loaded.name(), QString("My worlds"));
        QCOMPARE(loaded.items(), profile.items());
        QCOMPARE(loaded.items().size(), 3);
        QVERIFY(loaded.items().at(1).copyOnly);

        QVERIFY(loaded.removeItem("config/"));
        QCOMPARE(loaded.items().size(), 2);

        SyncProfile missing;
        QVERIFY(!SyncProfile::load(path("does-not-exist"), missing, &error));
    }

    void test_chooseMethod()
    {
        using SyncLink::Method;
#ifdef Q_OS_WIN
        QCOMPARE(SyncLink::chooseMethod(true, false, false), Method::Junction);
#else
        QCOMPARE(SyncLink::chooseMethod(true, false, false), Method::DirectorySymlink);
#endif
        QCOMPARE(SyncLink::chooseMethod(false, false, true), Method::FileSymlink);
        QCOMPARE(SyncLink::chooseMethod(false, false, false), Method::Copy);
        QCOMPARE(SyncLink::chooseMethod(true, true, true), Method::Copy);
        QCOMPARE(SyncLink::chooseMethod(false, true, true), Method::Copy);
    }

    void test_firstInstanceSeedsThenSecondJoins()
    {
        auto profile = makeProfile("seed");
        const auto a = path("seed-a/minecraft");
        const auto b = path("seed-b/minecraft");
        writeFile(a + "/saves/World/level.dat", "world from A");
        writeFile(a + "/config/mod.json", "{\"a\":1}");
        writeFile(a + "/options.txt", "fov:90");
        writeFile(b + "/saves/World/level.dat", "world from B");

        SyncEngine engineA(profile, a);
        engineA.setFileSymlinksAllowed(true);
        auto results = engineA.attach();
        QVERIFY(SyncEngine::allSucceeded(results));
        for (const auto& r : results) {
            QCOMPARE(r.status, SyncEngine::ItemResult::Status::Linked);
            QVERIFY(r.backupPath.isEmpty());
        }
        // A's content moved into the profile and A now links to it.
        QCOMPARE(readFile(profile.contentPath("saves/World/level.dat")), QByteArray("world from A"));
        QVERIFY(SyncLink::isLinkTo(a + "/saves/World", profile.contentPath("saves/World")));
        QVERIFY(SyncLink::isLinkTo(a + "/options.txt", profile.contentPath("options.txt")));
        QCOMPARE(readFile(a + "/config/mod.json"), QByteArray("{\"a\":1}"));

        // Attaching again is a no-op.
        QVERIFY(SyncEngine::allSucceeded(engineA.attach()));

        SyncEngine engineB(profile, b);
        engineB.setFileSymlinksAllowed(true);
        results = engineB.attach();
        QVERIFY(SyncEngine::allSucceeded(results));
        // B's own world was kept aside, never deleted.
        QCOMPARE(results.at(0).path, QString("saves/World"));
        QVERIFY(!results.at(0).backupPath.isEmpty());
        QCOMPARE(readFile(results.at(0).backupPath + "/level.dat"), QByteArray("world from B"));

        // Both instances now see the same files.
        QCOMPARE(readFile(b + "/saves/World/level.dat"), QByteArray("world from A"));
        writeFile(b + "/config/mod.json", "{\"b\":2}");
        QCOMPARE(readFile(a + "/config/mod.json"), QByteArray("{\"b\":2}"));

        // Minecraft is told to accept the linked worlds, once.
        engineB.attach();
        const auto allowed = QString::fromUtf8(readFile(b + "/allowed_symlinks.txt"));
        QCOMPARE(allowed.count("[prefix]"), 1);
        QVERIFY(allowed.contains(QDir::cleanPath(profile.contentPath())));
    }

    void test_fileFallsBackToCopy()
    {
        auto profile = makeProfile("copyfallback");
        const auto a = path("copy-a/minecraft");
        const auto b = path("copy-b/minecraft");
        writeFile(a + "/options.txt", "fov:70");

        SyncEngine engineA(profile, a);
        engineA.setFileSymlinksAllowed(false);  // e.g. Windows without Developer Mode
        QCOMPARE(engineA.methodFor(profile.items().at(2)), SyncLink::Method::Copy);
        QVERIFY(SyncEngine::allSucceeded(engineA.attach()));
        QVERIFY(!SyncLink::isLink(a + "/options.txt"));
        QCOMPARE(readFile(profile.contentPath("options.txt")), QByteArray("fov:70"));

        SyncEngine engineB(profile, b);
        engineB.setFileSymlinksAllowed(false);
        QVERIFY(SyncEngine::allSucceeded(engineB.attach()));
        QCOMPARE(readFile(b + "/options.txt"), QByteArray("fov:70"));

        // B plays and changes the file; finish() pushes it, A pulls it on its next launch.
        QThread::msleep(1100);  // file times can have 1 s resolution
        writeFile(b + "/options.txt", "fov:110");
        QVERIFY(SyncEngine::allSucceeded(engineB.finish()));
        QCOMPARE(readFile(profile.contentPath("options.txt")), QByteArray("fov:110"));
        QVERIFY(SyncEngine::allSucceeded(engineA.attach()));
        QCOMPARE(readFile(a + "/options.txt"), QByteArray("fov:110"));
    }

    void test_detachKeepsStandaloneCopy()
    {
        auto profile = makeProfile("detach");
        const auto a = path("detach-a/minecraft");
        writeFile(a + "/saves/World/level.dat", "shared world");

        SyncEngine engine(profile, a);
        engine.setFileSymlinksAllowed(true);
        QVERIFY(SyncEngine::allSucceeded(engine.attach()));
        QVERIFY(SyncLink::isLink(a + "/saves/World"));

        QString error;
        QVERIFY2(engine.detach(profile.items().at(0), &error), qPrintable(error));
        QVERIFY(!SyncLink::isLink(a + "/saves/World"));
        QCOMPARE(readFile(a + "/saves/World/level.dat"), QByteArray("shared world"));
        // The shared copy is untouched.
        QCOMPARE(readFile(profile.contentPath("saves/World/level.dat")), QByteArray("shared world"));
    }

    void test_nothingToSync()
    {
        auto profile = makeProfile("empty");
        SyncEngine engine(profile, path("empty-a/minecraft"));
        engine.setFileSymlinksAllowed(true);
        const auto results = engine.attach();
        QVERIFY(SyncEngine::allSucceeded(results));
        // Missing folders are created empty and linked; a missing file has nothing to link yet.
        QCOMPARE(results.at(0).status, SyncEngine::ItemResult::Status::Linked);
        QCOMPARE(results.at(2).status, SyncEngine::ItemResult::Status::NothingToSync);
    }

    void test_runningGuard()
    {
        const QList<SyncGuard::InstanceState> instances = {
            { "a", "Alpha", "shared", false },
            { "b", "Beta", "shared", true },
            { "c", "Gamma", "other", true },
            { "d", "Delta", "", true },
        };
        auto conflict = SyncGuard::findRunningConflict("a", "shared", instances);
        QVERIFY(conflict.has_value());
        QCOMPARE(conflict->name, QString("Beta"));

        // An instance never conflicts with itself, other profiles, or instances without a profile.
        QVERIFY(!SyncGuard::findRunningConflict("b", "shared", instances).has_value());
        QVERIFY(!SyncGuard::findRunningConflict("e", "nobody", instances).has_value());
        QVERIFY(!SyncGuard::findRunningConflict("e", "", instances).has_value());
    }
};

QTEST_GUILESS_MAIN(SyncTest)

#include "Sync_test.moc"
