#include <QTest>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

#include <tasks/ConcurrentTask.h>
#include <tasks/MultipleOptionsTask.h>
#include <tasks/SequentialTask.h>
#include <tasks/Task.h>

#include "LaunchController.h"
#include "InstanceTask.h"
#include "launch/LaunchTask.h"
#include "launch/TaskStepWrapper.h"
#include "launch/steps/LaunchCommand.h"
#include "minecraft/launch/LauncherPartLaunch.h"
#include "minecraft/launch/AutoInstallJava.h"
#include "minecraft/ComponentUpdateTask.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/MinecraftLoadAndCheck.h"
#include "minecraft/PackProfile.h"
#include "minecraft/VersionFile.h"
#include "settings/INISettingsObject.h"
#include "minecraft/mod/tasks/ResourceFolderLoadTask.h"
#include "modplatform/EnsureMetadataTask.h"
#include "modplatform/modrinth/ModrinthCheckUpdate.h"
#include "minecraft/update/AssetUpdateTask.h"
#include "minecraft/update/LibrariesTask.h"
#include "minecraft/update/LegacyFMLLibrariesTask.h"
#include "java/download/ArchiveDownloadTask.h"

#include <array>

/* Does nothing. Only used for testing. */
class BasicTask : public Task {
    Q_OBJECT

    friend class TaskTest;

   public:
    BasicTask(bool show_debug_log = true) : Task(show_debug_log) {}

   private:
    void executeTask() override { emitSucceeded(); }
};

/* Does nothing. Only used for testing. */
class BasicTask_MultiStep : public Task {
    Q_OBJECT

    friend class TaskTest;

   private:
    auto isMultiStep() const -> bool override { return true; }

    void executeTask() override {}
};

class BigConcurrentTask : public ConcurrentTask {
    Q_OBJECT

    void executeNextSubTask() override
    {
        // This is here only to help fill the stack a bit more quickly (if there's an issue, of course :^))
        // Each tasks thus adds 1024 * 4 bytes to the stack, at the very least.
        [[maybe_unused]] volatile std::array<uint32_t, 1024> some_data_on_the_stack{};

        ConcurrentTask::executeNextSubTask();
    }
};

class BigConcurrentTaskThread : public QThread {
    Q_OBJECT

    QTimer m_deadline;
    void run() override
    {
        BigConcurrentTask big_task;
        m_deadline.setInterval(10000);

        // NOTE: Arbitrary value that manages to trigger a problem when there is one.
        //       Considering each tasks, in a problematic state, adds 1024 * 4 bytes to the stack,
        //       this number is enough to fill up 16 MiB of stack, more than enough to cause a problem.
        static const unsigned s_num_tasks = 1 << 12;
        for (unsigned i = 0; i < s_num_tasks; i++) {
            auto sub_task = makeShared<BasicTask>(false);
            big_task.addTask(sub_task);
        }

        connect(&big_task, &Task::finished, this, &QThread::quit);
        connect(&m_deadline, &QTimer::timeout, this, [this] {
            passed_the_deadline = true;
            quit();
        });

        if (thread() != QThread::currentThread()) {
            QMetaObject::invokeMethod(this, &BigConcurrentTaskThread::start_timer, Qt::QueuedConnection);
        }
        big_task.run();

        exec();
    }
    void start_timer() { m_deadline.start(); }

   public:
    bool passed_the_deadline = false;
};

class SharedMetadataTask : public Task {
   public:
    int starts = 0;
    int startAttempts = 0;
    void start() override
    {
        ++startAttempts;
        // Count rejected attempts without relying on Debug-only assertions.
        if (!isRunning()) {
            Task::start();
        }
    }
    void complete() { emitSucceeded(); }
    void fail() { emitFailed("metadata unavailable"); }

   protected:
    void executeTask() override { ++starts; }
};

class OfflineComponentLoadTask : public ComponentUpdateTask {
   public:
    explicit OfflineComponentLoadTask(PackProfile* profile) : ComponentUpdateTask(Mode::Resolution, Net::Mode::Offline, profile) {}
    shared_qobject_ptr<SharedMetadataTask> metadata = makeShared<SharedMetadataTask>();
    int cacheMisses = 0;
    Net::Mode observedMode = Net::Mode::Online;

   protected:
    LoadResult loadComponent(ComponentPtr component, Task::Ptr& loadTask, Net::Mode mode) override
    {
        observedMode = mode;
        if (component->m_loaded) {
            return LoadResult::LoadedLocal;
        }
        if (component->getID() == "test.component") {
            ++cacheMisses;
            loadTask = metadata;
            return LoadResult::RequiresRemote;
        }
        component->m_loaded = true;
        return LoadResult::LoadedLocal;
    }
};

class TerminalSignalTask : public Task {
   public:
    using Task::emitAborted;
    using Task::emitFailed;
    using Task::emitFailedWithCode;
    using Task::emitSucceeded;

   protected:
    void executeTask() override {}
};

class ControlledAbortTask : public Task {
   public:
    ControlledAbortTask(bool acceptsAbort, bool finishesOnAbort) : m_acceptsAbort(acceptsAbort), m_finishesOnAbort(finishesOnAbort) {}
    int starts = 0;
    int aborts = 0;
    std::function<void()> beforeAbort;
    void complete() { emitSucceeded(); }
    bool abort() override
    {
        ++aborts;
        if (beforeAbort) {
            beforeAbort();
        }
        if (m_finishesOnAbort) {
            emitAborted();
        }
        return m_acceptsAbort;
    }

   protected:
    void executeTask() override { ++starts; }

   private:
    bool m_acceptsAbort;
    bool m_finishesOnAbort;
};

class LateGrandchildTask : public ConcurrentTask {
   public:
    explicit LateGrandchildTask(Task::Ptr child) : m_child(child) {}

   protected:
    void executeTask() override
    {
        addTask(m_child);
        ConcurrentTask::executeTask();
    }

   private:
    Task::Ptr m_child;
};

template <typename Base>
class IdleAbortWrapper : public Base {
   public:
    using Base::Base;
    void complete() { this->emitSucceeded(); }

   protected:
    void executeTask() override {}
};

class PolicyTrackedTask : public ConcurrentTask {
   public:
    bool recordedFailure(const Task::Ptr& task) const { return m_failed.contains(task.get()); }
};

class FailureHookTask : public ConcurrentTask {
   public:
    int failureHooks = 0;

   protected:
    void emitFailed(QString reason) override
    {
        ++failureHooks;
        ConcurrentTask::emitFailed(reason);
    }
};

class EmptyLaunchOwner : public LaunchTask {
   public:
    explicit EmptyLaunchOwner(MinecraftInstance* instance = nullptr) : LaunchTask(instance) {}
};

class EnvironmentlessInstance : public MinecraftInstance {
   public:
    using MinecraftInstance::MinecraftInstance;
    QProcessEnvironment createEnvironment() override { return {}; }
    void loadSpecificSettings() override {}
};

class TestJavaInstall : public AutoInstallJava {
   public:
    TestJavaInstall(LaunchTask* owner, Task::Ptr child) : AutoInstallJava(owner) { m_current_task = child; }

   protected:
    void executeTask() override {}
};

class TaskTest : public QObject {
    Q_OBJECT

   private slots:
    void test_instanceInteractionFollowsTaskPolicy()
    {
        IdleAbortWrapper<InstanceTask> task;
        QVERIFY(task.isInteractive());
        QVERIFY(task.shouldConfirmUpdate());
        QVERIFY(task.setUnattended(true));
        task.setInteractive(true);
        task.setConfirmUpdate(true);
        QVERIFY(!task.isInteractive());
        QVERIFY(!task.shouldConfirmUpdate());
        QVERIFY(task.setUnattended(false));
        QVERIFY(task.isInteractive());
        QVERIFY(task.shouldConfirmUpdate());
    }

    void test_automaticVersionSelectionRejectsUnattendedExecution()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        INISettingsObject globals(dir.filePath("global.cfg"));
        for (const auto* name :
             { "ShowGameTime", "RecordGameTime", "PreLoadCommand", "PreLaunchCommand", "WrapperCommand", "PostExitCommand", "ShowConsole",
               "AutoCloseConsole", "ShowConsoleOnError", "LogPrePostOutput", "ConsoleMaxLines", "ConsoleOverflowStop" }) {
            globals.registerSetting(name, false);
        }
        auto settings = std::make_unique<INISettingsObject>(dir.filePath("instance.cfg"));
        settings->registerSetting("UseLatestMinecraftVersion", true);
        EnvironmentlessInstance instance(&globals, std::move(settings), dir.path());
        MinecraftLoadAndCheck task(&instance, Net::Mode::Online);
        QVERIFY(task.setUnattended(true));
        QSignalSpy failed(&task, &Task::failed);
        task.start();
        QCOMPARE(task.failureCode(), Task::FailureCode::UnsupportedUnattended);
        QCOMPARE(failed.count(), 1);
        QVERIFY(task.isFinished());
    }

    void test_launchProcessesRefuseAbortBeforeStarting()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        INISettingsObject globals(dir.filePath("global.cfg"));
        for (const auto* name :
             { "ShowGameTime", "RecordGameTime", "PreLoadCommand", "PreLaunchCommand", "WrapperCommand", "PostExitCommand", "ShowConsole",
               "AutoCloseConsole", "ShowConsoleOnError", "LogPrePostOutput", "ConsoleMaxLines", "ConsoleOverflowStop" }) {
            globals.registerSetting(name, false);
        }
        auto settings = std::make_unique<INISettingsObject>(dir.filePath("instance.cfg"));
        settings->registerSetting("JavaVersion", "17");
        settings->registerSetting("CloseAfterLaunch", false);
        EnvironmentlessInstance instance(&globals, std::move(settings), dir.path());
        EmptyLaunchOwner launch(&instance);
        IdleAbortWrapper<LaunchCommand> command(&launch, QString());
        IdleAbortWrapper<LauncherPartLaunch> game(&launch);
        for (Task* task : { static_cast<Task*>(&command), static_cast<Task*>(&game) }) {
            disconnect(task, nullptr, &launch, nullptr);
            task->start();
            QSignalSpy finished(task, &Task::finished);
            QVERIFY(!task->abort());
            QVERIFY(task->isRunning());
            QCOMPARE(finished.count(), 0);
        }
        command.complete();
        game.complete();
    }

    void test_javaInstallRefusedAbortKeepsRunning()
    {
        EmptyLaunchOwner launch;
        auto child = makeShared<ControlledAbortTask>(false, false);
        child->setAbortable(true);
        TestJavaInstall task(&launch, child);
        disconnect(&task, nullptr, &launch, nullptr);
        child->start();
        task.start();
        QSignalSpy finished(&task, &Task::finished);
        QVERIFY(!task.abort());
        QVERIFY(task.isRunning());
        QVERIFY(child->isRunning());
        QCOMPARE(child->aborts, 1);
        QCOMPARE(finished.count(), 0);
    }

    void test_offlineMetadataCacheMiss_data()
    {
        QTest::addColumn<bool>("cached");
        QTest::addColumn<bool>("metadataSucceeds");
        QTest::addColumn<bool>("cancel");
        QTest::newRow("cache-miss-success") << false << true << false;
        QTest::newRow("cache-miss-failure") << false << false << false;
        QTest::newRow("already-cached") << true << true << false;
        QTest::newRow("cancel-pending-load") << false << false << true;
    }

    void test_offlineMetadataCacheMiss()
    {
        QFETCH(bool, cached);
        QFETCH(bool, metadataSucceeds);
        QFETCH(bool, cancel);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        INISettingsObject globals(dir.filePath("global.cfg"));
        for (const auto* name :
             { "ShowGameTime", "RecordGameTime", "PreLoadCommand", "PreLaunchCommand", "WrapperCommand", "PostExitCommand", "ShowConsole",
               "AutoCloseConsole", "ShowConsoleOnError", "LogPrePostOutput", "ConsoleMaxLines", "ConsoleOverflowStop" }) {
            globals.registerSetting(name, false);
        }
        auto settings = std::make_unique<INISettingsObject>(dir.filePath("instance.cfg"));
        settings->registerSetting("UseLatestMinecraftVersion", false);
        MinecraftInstance instance(&globals, std::move(settings), dir.path());
        auto* profile = instance.getPackProfile();
        auto component = makeShared<Component>(profile, "test.component");
        component->m_loaded = cached;
        auto payload = std::make_shared<VersionFile>();
        payload->uid = "test.component";
        payload->version = "1";
        payload->m_requires.insert(Meta::Require{ .uid = "missing.dependency", .equalsVersion = "1" });
        if (cached) {
            component->m_file = payload;
        }
        profile->appendComponent(component);
        OfflineComponentLoadTask operation(profile);
        QVERIFY(operation.setUnattended(true));
        if (!cached) {
            connect(operation.metadata.get(), &Task::succeeded, &operation, [component, payload] { component->m_file = payload; });
        }
        QSignalSpy finished(&operation, &Task::finished);
        operation.start();
        QCOMPARE(operation.observedMode, Net::Mode::Offline);
        if (!cached) {
            QVERIFY(operation.isRunning());
            QCOMPARE(finished.count(), 0);
            QCOMPARE(operation.metadata->startAttempts, 1);
            QVERIFY(operation.metadata->isUnattended());
            if (cancel) {
                operation.metadata->setAbortable(true);
                QSignalSpy failed(&operation, &Task::failed);
                QSignalSpy aborted(&operation, &Task::aborted);
                QVERIFY(operation.abort());
                QCOMPARE(operation.getState(), Task::State::AbortedByUser);
                QCOMPARE(operation.failureCode(), Task::FailureCode::Cancelled);
                QCOMPARE(aborted.count(), 1);
                QCOMPARE(failed.count(), 0);
                QCOMPARE(finished.count(), 1);
                return;
            }
            if (metadataSucceeds) {
                operation.metadata->complete();
            } else {
                operation.metadata->fail();
            }
        }
        QCOMPARE(finished.count(), 1);
        QCOMPARE(operation.getState(), Task::State::Failed);
        QCOMPARE(operation.cacheMisses, cached ? 0 : 1);
        // Offline resolution must report the unmet requirement without adding it.
        QVERIFY(!profile->getComponent("missing.dependency"));
        if (metadataSucceeds) {
            QVERIFY(operation.failReason().contains("unresolved dependencies"));
        } else {
            QVERIFY(operation.failReason().contains("metadata"));
        }
    }

    void test_waitingLaunchWrapperCancelsWithoutStartingChild()
    {
        EmptyLaunchOwner launch;
        auto child = makeShared<ConcurrentTask>();
        auto step = makeShared<TaskStepWrapper>(&launch, child);
        // Exercise the wrapper independently of the full launch state machine.
        disconnect(step.get(), nullptr, &launch, nullptr);
        ConcurrentTask parent;
        QVERIFY(parent.addTask(step));
        parent.start();
        QTRY_VERIFY(step->isRunning());
        QCOMPARE(child->getState(), Task::State::Inactive);
        QSignalSpy finished(&parent, &Task::finished);
        QVERIFY(parent.abort());
        QCOMPARE(step->getState(), Task::State::AbortedByUser);
        QCOMPARE(parent.getState(), Task::State::AbortedByUser);
        QCOMPARE(finished.count(), 1);
        step->proceed();
        QCoreApplication::processEvents();
        QCOMPARE(child->getState(), Task::State::Inactive);
        QCOMPARE(finished.count(), 1);
    }

    void test_inactiveTerminalEmissionIsRejected()
    {
        if (qEnvironmentVariableIsSet("PRISM_TEST_INACTIVE_TASK_EMIT")) {
            TerminalSignalTask task;
            QSignalSpy finished(&task, &Task::finished);
            task.emitFailed("inactive task");
            // Release builds log and reject the transition; Debug builds assert.
            QCOMPARE(task.getState(), Task::State::Inactive);
            QCOMPARE(finished.count(), 0);
            return;
        }
        QProcess child;
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("PRISM_TEST_INACTIVE_TASK_EMIT", "1");
        child.setProcessEnvironment(environment);
        child.start(QCoreApplication::applicationFilePath(), { "Task_test", "test_inactiveTerminalEmissionIsRejected" });
        QVERIFY(child.waitForStarted());
        QVERIFY(child.waitForFinished(15000));
        const auto output = child.readAllStandardOutput() + child.readAllStandardError();
        if (child.exitStatus() == QProcess::CrashExit) {
            QVERIFY2(output.contains("ASSERT") && output.contains("Task.cpp"), output.constData());
        } else {
            QCOMPARE(child.exitCode(), 0);
            QVERIFY2(output.contains("failed while not running"), output.constData());
        }
    }

    void test_inactiveAndFinishedAbortIsNoOp()
    {
        ConcurrentTask parent;
        auto child = makeShared<ControlledAbortTask>(true, true);
        QVERIFY(parent.addTask(child));
        QSignalSpy finished(&parent, &Task::finished);
        QVERIFY(parent.abort());
        QCOMPARE(parent.getState(), Task::State::Inactive);
        QCOMPARE(finished.count(), 0);
        parent.start();
        QTRY_VERIFY(child->isRunning());
        child->complete();
        QTRY_VERIFY(parent.wasSuccessful());
        QCOMPARE(child->starts, 1);
        QVERIFY(parent.abort());
        QCOMPARE(parent.getState(), Task::State::Succeeded);
        QCOMPARE(finished.count(), 1);
    }

    void test_failedAbortIsAlsoNoOpAfterCompletion()
    {
        ConcurrentTask parent;
        auto child = makeShared<ControlledAbortTask>(false, true);
        QVERIFY(parent.addTask(child));
        parent.start();
        QTRY_VERIFY(child->isRunning());
        QSignalSpy finished(&parent, &Task::finished);
        QVERIFY(!parent.abort());
        QCOMPARE(parent.getState(), Task::State::Failed);
        const auto reason = parent.failReason();
        QVERIFY(parent.abort());
        QCOMPARE(parent.getState(), Task::State::Failed);
        QCOMPARE(parent.failReason(), reason);
        QCOMPARE(finished.count(), 1);
    }

    void test_cancelledFolderScanHasTerminalState()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        auto child = makeShared<ResourceFolderLoadTask>(QDir(dir.path()), QDir(dir.path()), false, false,
                                                        [](const QFileInfo& info) { return new Resource(info); });
        ConcurrentTask parent;
        QVERIFY(parent.addTask(child));
        bool accepted = false;
        connect(child.get(), &Task::started, &parent, [&] { accepted = parent.abort(); });
        QSignalSpy childFinished(child.get(), &Task::finished);
        QSignalSpy parentFinished(&parent, &Task::finished);
        parent.start();
        QTRY_VERIFY(parent.isFinished());
        QVERIFY(accepted);
        QCOMPARE(child->getState(), Task::State::AbortedByUser);
        QCOMPARE(child->failureCode(), Task::FailureCode::Cancelled);
        QCOMPARE(parent.getState(), Task::State::AbortedByUser);
        QCOMPARE(childFinished.count(), 1);
        QCOMPARE(parentFinished.count(), 1);
    }

    void test_policyRejectionIsRecordedAndWaitsForOwnedWork()
    {
        PolicyTrackedTask parent;
        QVERIFY(parent.setUnattended(true));
        auto owned = makeShared<ControlledAbortTask>(true, false);
        auto unrelated = makeShared<ControlledAbortTask>(true, true);
        QVERIFY(parent.addTask(owned));
        parent.start();
        QTRY_VERIFY(owned->isRunning());
        QVERIFY(parent.addTask(unrelated));
        QVERIFY(unrelated->setUnattended(false));
        unrelated->start();
        QSignalSpy finished(&parent, &Task::finished);
        QVERIFY(QMetaObject::invokeMethod(&parent, "executeNextSubTask", Qt::QueuedConnection));
        QTRY_VERIFY(parent.recordedFailure(unrelated));
        QVERIFY(parent.isRunning());
        QCOMPARE(finished.count(), 0);
        QCOMPARE(owned->aborts, 1);
        QCOMPARE(unrelated->aborts, 0);
        QVERIFY(unrelated->isRunning());
        owned->complete();
        QCOMPARE(parent.failureCode(), Task::FailureCode::PolicyConflict);
        QVERIFY(parent.failReason().contains("interaction policy"));
        QCOMPARE(finished.count(), 1);
        unrelated->complete();
        QCOMPARE(finished.count(), 1);
    }

    void test_compositeStructuredFailureHook_data()
    {
        QTest::addColumn<int>("children");
        QTest::newRow("single-structured-failure") << 1;
        QTest::newRow("aggregate-failures") << 2;
    }

    void test_compositeStructuredFailureHook()
    {
        QFETCH(int, children);
        FailureHookTask parent;
        QList<shared_qobject_ptr<TerminalSignalTask>> tasks;
        for (int i = 0; i < children; ++i) {
            auto child = makeShared<TerminalSignalTask>();
            tasks.append(child);
            QVERIFY(parent.addTask(child));
        }
        parent.start();
        for (const auto& child : tasks) {
            QTRY_VERIFY(child->isRunning());
        }
        tasks[0]->emitFailedWithCode(Task::FailureCode::NotFound, "not found");
        if (children > 1) {
            tasks[1]->emitFailedWithCode(Task::FailureCode::Timeout, "timed out");
        }
        QTRY_VERIFY(parent.isFinished());
        QCOMPARE(parent.failureCode(), children == 1 ? Task::FailureCode::NotFound : Task::FailureCode::Failed);
        QCOMPARE(parent.failureHooks, children == 1 ? 0 : 1);
    }

    void test_compositePreservesFailureCode_data()
    {
        QTest::addColumn<bool>("sequential");
        QTest::newRow("concurrent") << false;
        QTest::newRow("resource-download-style") << true;
    }

    void test_compositePreservesFailureCode()
    {
        QFETCH(bool, sequential);
        shared_qobject_ptr<ConcurrentTask> parent = makeShared<ConcurrentTask>();
        if (sequential) {
            parent = makeShared<SequentialTask>();
        }
        auto child = makeShared<TerminalSignalTask>();
        int cleanups = 0;
        // ResourceDownloadTask performs cleanup before the composite failure handler.
        connect(child.get(), &Task::failed, parent.get(), [&cleanups] { ++cleanups; });
        QVERIFY(parent->addTask(child));
        QSignalSpy failed(parent.get(), &Task::failed);
        QSignalSpy finished(parent.get(), &Task::finished);
        parent->start();
        QTRY_VERIFY(child->isRunning());
        child->emitFailedWithCode(Task::FailureCode::NotFound, "typed child failure");
        QTRY_VERIFY(parent->isFinished());
        QCOMPARE(parent->failureCode(), Task::FailureCode::NotFound);
        QCOMPARE(parent->failReason(), QString("typed child failure"));
        QCOMPARE(failed.count(), 1);
        QCOMPARE(finished.count(), 1);
        QCOMPARE(cleanups, 1);
    }

    void test_launchPreparationDoesNotClaimCancellation()
    {
        auto child = makeShared<IdleAbortWrapper<LaunchController>>();
        ConcurrentTask parent;
        QVERIFY(parent.addTask(child));
        parent.start();
        QTRY_VERIFY(child->isRunning());
        QSignalSpy finished(&parent, &Task::finished);
        QVERIFY(!parent.abort());
        QVERIFY(parent.isRunning());
        QCOMPARE(finished.count(), 0);
        child->complete();
        QCOMPARE(parent.getState(), Task::State::Failed);
        QCOMPARE(finished.count(), 1);
    }

    void test_idleDownloadWrapperAbort_data()
    {
        QTest::addColumn<int>("kind");
        QTest::newRow("assets") << 0;
        QTest::newRow("libraries") << 1;
        QTest::newRow("legacy-libraries") << 2;
        QTest::newRow("java-archive") << 3;
        QTest::newRow("component-coordinator") << 4;
        QTest::newRow("metadata-generator") << 5;
        QTest::newRow("modrinth-update") << 6;
    }

    void test_idleDownloadWrapperAbort()
    {
        QFETCH(int, kind);
        Task::Ptr child;
        QList<Resource*> resources;
        std::vector<Version> versions;
        switch (kind) {
            case 0:
                child = makeShared<IdleAbortWrapper<AssetUpdateTask>>(nullptr);
                break;
            case 1:
                child = makeShared<IdleAbortWrapper<LibrariesTask>>(nullptr);
                break;
            case 2:
                child = makeShared<IdleAbortWrapper<LegacyFMLLibrariesTask>>(nullptr);
                break;
            case 3:
                child = makeShared<IdleAbortWrapper<Java::ArchiveDownloadTask>>(QUrl{}, QString{});
                break;
            case 4:
                child =
                    makeShared<IdleAbortWrapper<ComponentUpdateTask>>(ComponentUpdateTask::Mode::Resolution, Net::Mode::Offline, nullptr);
                break;
            case 5:
                child = makeShared<IdleAbortWrapper<EnsureMetadataTask>>(static_cast<Resource*>(nullptr), QDir{});
                break;
            default:
                child =
                    makeShared<IdleAbortWrapper<ModrinthCheckUpdate>>(resources, versions, QList<ModPlatform::ModLoaderType>{}, nullptr);
                break;
        }
        ConcurrentTask parent;
        QVERIFY(parent.addTask(child));
        parent.start();
        QTRY_VERIFY(child->isRunning());
        QSignalSpy finished(&parent, &Task::finished);
        QVERIFY(parent.abort());
        QCOMPARE(parent.getState(), Task::State::AbortedByUser);
        QCOMPARE(child->getState(), Task::State::AbortedByUser);
        QCOMPARE(finished.count(), 1);
    }

    void test_abortToleratesSiblingCompletion()
    {
        ConcurrentTask parent;
        auto first = makeShared<ControlledAbortTask>(true, true);
        auto second = makeShared<ControlledAbortTask>(true, true);
        first->beforeAbort = [pointer = second.get()] {
            if (pointer->isRunning())
                pointer->complete();
        };
        second->beforeAbort = [pointer = first.get()] {
            if (pointer->isRunning())
                pointer->complete();
        };
        QVERIFY(parent.addTask(first));
        QVERIFY(parent.addTask(second));
        parent.start();
        QTRY_VERIFY(first->isRunning() && second->isRunning());
        QSignalSpy finished(&parent, &Task::finished);
        QVERIFY(parent.abort());
        QCOMPARE(parent.getState(), Task::State::AbortedByUser);
        QCOMPARE(finished.count(), 1);
        QCOMPARE(first->aborts + second->aborts, 1);
    }

    void test_orderedCompositeAbort_data()
    {
        QTest::addColumn<bool>("multipleOptions");
        QTest::addColumn<bool>("active");
        QTest::newRow("sequential-queued") << false << false;
        QTest::newRow("sequential-active") << false << true;
        QTest::newRow("options-queued") << true << false;
        QTest::newRow("options-active") << true << true;
    }

    void test_orderedCompositeAbort()
    {
        QFETCH(bool, multipleOptions);
        QFETCH(bool, active);
        shared_qobject_ptr<ConcurrentTask> parent = makeShared<SequentialTask>();
        if (multipleOptions) {
            parent = makeShared<MultipleOptionsTask>("options");
        }
        auto child = makeShared<ControlledAbortTask>(true, false);
        QVERIFY(parent->addTask(child));
        QSignalSpy finished(parent.get(), &Task::finished);
        parent->start();
        if (active) {
            QTRY_VERIFY(child->isRunning());
        }
        QVERIFY(parent->abort());
        QVERIFY(QMetaObject::invokeMethod(parent.get(), "executeNextSubTask", Qt::QueuedConnection));
        QCoreApplication::processEvents();
        QCOMPARE(finished.count(), active ? 0 : 1);
        if (active) {
            QVERIFY(parent->isRunning());
            child->complete();
        }
        QCOMPARE(parent->getState(), Task::State::AbortedByUser);
        QCOMPARE(finished.count(), 1);
    }

    void test_unattendedReachesLateGrandchild()
    {
        ConcurrentTask operation;
        ConcurrentTask unrelated;
        auto leaf = makeShared<SharedMetadataTask>();
        auto middle = makeShared<LateGrandchildTask>(leaf);
        QVERIFY(operation.addTask(middle));
        QVERIFY(operation.setUnattended(true));
        QVERIFY(middle->isUnattended());
        operation.start();
        QTRY_VERIFY(leaf->isRunning());
        QVERIFY(leaf->isUnattended());
        QVERIFY(!operation.setUnattended(false));
        QVERIFY(!leaf->setUnattended(false));
        QVERIFY(operation.isUnattended());
        QVERIFY(leaf->isUnattended());
        QVERIFY(!unrelated.isUnattended());
        leaf->complete();
        QTRY_VERIFY(operation.wasSuccessful());
    }

    void test_unattendedRejectsRunningInteractiveChild()
    {
        ConcurrentTask operation;
        QVERIFY(operation.setUnattended(true));
        auto existing = makeShared<SharedMetadataTask>();
        existing->start();
        QVERIFY(!operation.addTask(existing));
        QVERIFY(!existing->isUnattended());
        QVERIFY(existing->isRunning());
        existing->complete();
    }

    void test_abortBeforeChildrenStart()
    {
        ConcurrentTask parent;
        auto child = makeShared<ControlledAbortTask>(true, true);
        parent.addTask(child);
        QSignalSpy finished(&parent, &Task::finished);
        parent.start();
        QVERIFY(parent.abort());
        QCOMPARE(parent.getState(), Task::State::AbortedByUser);
        QCOMPARE(finished.count(), 1);
        QCoreApplication::processEvents();
        QCOMPARE(child->starts, 0);
        QCOMPARE(child->aborts, 0);
    }

    void test_abortWaitsForAllChildren_data()
    {
        QTest::addColumn<bool>("acceptsAbort");
        QTest::addColumn<bool>("finishesOnAbort");
        QTest::newRow("immediate") << true << true;
        QTest::newRow("delayed") << true << false;
        QTest::newRow("refused") << false << false;
        QTest::newRow("failed-after-stopping") << false << true;
    }

    void test_abortWaitsForAllChildren()
    {
        QFETCH(bool, acceptsAbort);
        QFETCH(bool, finishesOnAbort);
        ConcurrentTask parent("abort", 2);
        auto first = makeShared<ControlledAbortTask>(true, true);
        auto second = makeShared<ControlledAbortTask>(acceptsAbort, finishesOnAbort);
        auto queued = makeShared<ControlledAbortTask>(true, true);
        parent.addTask(first);
        parent.addTask(second);
        parent.addTask(queued);
        parent.start();
        QTRY_VERIFY(first->isRunning() && second->isRunning());
        QSignalSpy finished(&parent, &Task::finished);
        QCOMPARE(parent.abort(), acceptsAbort);
        QCOMPARE(first->aborts, 1);
        QCOMPARE(second->aborts, 1);
        if (!finishesOnAbort) {
            QVERIFY(parent.isRunning());
            QCOMPARE(finished.count(), 0);
            second->complete();
        }
        QCOMPARE(finished.count(), 1);
        QCOMPARE(parent.getState(), acceptsAbort ? Task::State::AbortedByUser : Task::State::Failed);
        QCoreApplication::processEvents();
        QCOMPARE(queued->starts, 0);
        QCOMPARE(finished.count(), 1);
    }

    void test_terminalSignalsAreIdempotent()
    {
        for (int first = 0; first < 3; ++first) {
            TerminalSignalTask task;
            QSignalSpy finished(&task, &Task::finished);
            QSignalSpy succeeded(&task, &Task::succeeded);
            QSignalSpy failed(&task, &Task::failed);
            QSignalSpy aborted(&task, &Task::aborted);
            task.start();
            if (first == 0) {
                task.emitSucceeded();
            } else if (first == 1) {
                task.emitFailed("first failure");
            } else {
                task.emitAborted();
            }
            const auto state = task.getState();
            const auto reason = task.failReason();
            task.emitFailed("late failure");
            task.emitAborted();
            task.emitSucceeded();
            QCOMPARE(task.getState(), state);
            QCOMPARE(task.failReason(), reason);
            QCOMPARE(finished.count(), 1);
            QCOMPARE(succeeded.count(), first == 0 ? 1 : 0);
            QCOMPARE(failed.count(), first == 1 ? 1 : 0);
            QCOMPARE(aborted.count(), first == 2 ? 1 : 0);
        }
    }

    void test_sharedMetadataTaskStartsOnce_data()
    {
        QTest::addColumn<bool>("alreadyRunning");
        QTest::newRow("queued-by-both-parents") << false;
        QTest::newRow("already-running") << true;
    }

    void test_sharedMetadataTaskStartsOnce()
    {
        QFETCH(bool, alreadyRunning);
        auto child = makeShared<SharedMetadataTask>();
        ConcurrentTask first;
        ConcurrentTask second;
        first.addTask(child);
        second.addTask(child);
        if (alreadyRunning) {
            // Delay completion until both composites have attached their handlers.
            child->start();
        }
        first.start();
        second.start();
        QTRY_VERIFY(child->isRunning() && !first.getStepProgress().isEmpty() && !second.getStepProgress().isEmpty());
        QCoreApplication::processEvents();
        child->complete();
        QTRY_VERIFY(first.isFinished() && second.isFinished());
        QVERIFY(first.wasSuccessful());
        QVERIFY(second.wasSuccessful());
        QCOMPARE(child->starts, 1);
        QCOMPARE(child->startAttempts, 1);
    }

    void test_SetStatus_NoMultiStep()
    {
        BasicTask t;
        QString status{ "test status" };

        t.setStatus(status);

        QCOMPARE(t.getStatus(), status);
        QCOMPARE(t.getStepProgress().isEmpty(), TaskStepProgressList{}.isEmpty());
    }

    void test_SetStatus_MultiStep()
    {
        BasicTask_MultiStep t;
        QString status{ "test status" };

        t.setStatus(status);

        QCOMPARE(t.getStatus(), status);
        // Even though it is multi step, it does not override the getStepStatus method,
        // so it should remain the same.
        QCOMPARE(t.getStepProgress().isEmpty(), TaskStepProgressList{}.isEmpty());
    }

    void test_SetProgress()
    {
        BasicTask t;
        int current = 42;
        int total = 207;

        t.setProgress(current, total);

        QCOMPARE(t.getProgress(), current);
        QCOMPARE(t.getTotalProgress(), total);
    }

    void test_basicRun()
    {
        BasicTask t;
        connect(&t, &Task::finished, &t,
                [&t] { QVERIFY2(t.wasSuccessful(), "Task finished but was not successful when it should have been."); });
        t.start();

        QVERIFY2(QTest::qWaitFor([&t]() { return t.isFinished(); }, 1000), "Task didn't finish as it should.");
    }

    void test_basicConcurrentRun()
    {
        auto t1 = makeShared<BasicTask>();
        auto t2 = makeShared<BasicTask>();
        auto t3 = makeShared<BasicTask>();

        ConcurrentTask t;

        t.addTask(t1);
        t.addTask(t2);
        t.addTask(t3);

        connect(&t, &Task::finished, &t, [&t, &t1, &t2, &t3] {
            QVERIFY2(t.wasSuccessful(), "Task finished but was not successful when it should have been.");
            QVERIFY(t1->wasSuccessful());
            QVERIFY(t2->wasSuccessful());
            QVERIFY(t3->wasSuccessful());
        });

        t.start();
        QVERIFY2(QTest::qWaitFor([&t]() { return t.isFinished(); }, 1000), "Task didn't finish as it should.");
    }

    // Tests if starting new tasks after the 6 initial ones is working
    void test_moreConcurrentRun()
    {
        auto t1 = makeShared<BasicTask>();
        auto t2 = makeShared<BasicTask>();
        auto t3 = makeShared<BasicTask>();
        auto t4 = makeShared<BasicTask>();
        auto t5 = makeShared<BasicTask>();
        auto t6 = makeShared<BasicTask>();
        auto t7 = makeShared<BasicTask>();
        auto t8 = makeShared<BasicTask>();
        auto t9 = makeShared<BasicTask>();

        ConcurrentTask t;

        t.addTask(t1);
        t.addTask(t2);
        t.addTask(t3);
        t.addTask(t4);
        t.addTask(t5);
        t.addTask(t6);
        t.addTask(t7);
        t.addTask(t8);
        t.addTask(t9);

        connect(&t, &Task::finished, &t, [&t, &t1, &t2, &t3, &t4, &t5, &t6, &t7, &t8, &t9] {
            QVERIFY2(t.wasSuccessful(), "Task finished but was not successful when it should have been.");
            QVERIFY(t1->wasSuccessful());
            QVERIFY(t2->wasSuccessful());
            QVERIFY(t3->wasSuccessful());
            QVERIFY(t4->wasSuccessful());
            QVERIFY(t5->wasSuccessful());
            QVERIFY(t6->wasSuccessful());
            QVERIFY(t7->wasSuccessful());
            QVERIFY(t8->wasSuccessful());
            QVERIFY(t9->wasSuccessful());
        });

        t.start();
        QVERIFY2(QTest::qWaitFor([&t]() { return t.isFinished(); }, 1000), "Task didn't finish as it should.");
    }

    void test_basicSequentialRun()
    {
        auto t1 = makeShared<BasicTask>();
        auto t2 = makeShared<BasicTask>();
        auto t3 = makeShared<BasicTask>();

        SequentialTask t;

        t.addTask(t1);
        t.addTask(t2);
        t.addTask(t3);

        connect(&t, &Task::finished, &t, [&t, &t1, &t2, &t3] {
            QVERIFY2(t.wasSuccessful(), "Task finished but was not successful when it should have been.");
            QVERIFY(t1->wasSuccessful());
            QVERIFY(t2->wasSuccessful());
            QVERIFY(t3->wasSuccessful());
        });

        t.start();
        QVERIFY2(QTest::qWaitFor([&t]() { return t.isFinished(); }, 1000), "Task didn't finish as it should.");
    }

    void test_basicMultipleOptionsRun()
    {
        auto t1 = makeShared<BasicTask>();
        auto t2 = makeShared<BasicTask>();
        auto t3 = makeShared<BasicTask>();

        MultipleOptionsTask t;

        t.addTask(t1);
        t.addTask(t2);
        t.addTask(t3);

        connect(&t, &Task::finished, &t, [&t, &t1, &t2, &t3] {
            QVERIFY2(t.wasSuccessful(), "Task finished but was not successful when it should have been.");
            QVERIFY(t1->wasSuccessful());
            QVERIFY(!t2->wasSuccessful());
            QVERIFY(!t3->wasSuccessful());
        });

        t.start();
        QVERIFY2(QTest::qWaitFor([&t]() { return t.isFinished(); }, 1000), "Task didn't finish as it should.");
    }

    void test_stackOverflowInConcurrentTask()
    {
        BigConcurrentTaskThread thread;

        thread.start();

        QVERIFY2(QTest::qWaitFor([&thread] { return thread.isFinished(); }, 15000), "Thread didn't finish as it should.");

        QVERIFY(!thread.passed_the_deadline);
    }
};

QTEST_GUILESS_MAIN(TaskTest)

#include "Task_test.moc"
