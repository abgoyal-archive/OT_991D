

#include "config.h"
#include "DatabaseThread.h"

#if ENABLE(DATABASE)

#include "AutodrainedPool.h"
#include "Database.h"
#include "DatabaseTask.h"
#include "Logging.h"
#include "SQLTransactionClient.h"
#include "SQLTransactionCoordinator.h"

namespace WebCore {

DatabaseThread::DatabaseThread()
    : m_threadID(0)
    , m_transactionClient(new SQLTransactionClient())
    , m_transactionCoordinator(new SQLTransactionCoordinator())
    , m_cleanupSync(0)
{
    m_selfRef = this;
}

DatabaseThread::~DatabaseThread()
{
    // FIXME: Any cleanup required here?  Since the thread deletes itself after running its detached course, I don't think so.  Lets be sure.
    ASSERT(terminationRequested());
}

bool DatabaseThread::start()
{
    MutexLocker lock(m_threadCreationMutex);

    if (m_threadID)
        return true;

    m_threadID = createThread(DatabaseThread::databaseThreadStart, this, "WebCore: Database");

    return m_threadID;
}

void DatabaseThread::requestTermination(DatabaseTaskSynchronizer *cleanupSync)
{
    ASSERT(!m_cleanupSync);
    m_cleanupSync = cleanupSync;
    LOG(StorageAPI, "DatabaseThread %p was asked to terminate\n", this);
    m_queue.kill();
}

bool DatabaseThread::terminationRequested() const
{
    return m_queue.killed();
}

void* DatabaseThread::databaseThreadStart(void* vDatabaseThread)
{
    DatabaseThread* dbThread = static_cast<DatabaseThread*>(vDatabaseThread);
    return dbThread->databaseThread();
}

void* DatabaseThread::databaseThread()
{
    {
        // Wait for DatabaseThread::start() to complete.
        MutexLocker lock(m_threadCreationMutex);
        LOG(StorageAPI, "Started DatabaseThread %p", this);
    }

    AutodrainedPool pool;
    while (OwnPtr<DatabaseTask> task = m_queue.waitForMessage()) {
        task->performTask();
        pool.cycle();
    }

    // Clean up the list of all pending transactions on this database thread
    m_transactionCoordinator->shutdown();

    LOG(StorageAPI, "About to detach thread %i and clear the ref to DatabaseThread %p, which currently has %i ref(s)", m_threadID, this, refCount());

    // Close the databases that we ran transactions on. This ensures that if any transactions are still open, they are rolled back and we don't leave the database in an
    // inconsistent or locked state.
    if (m_openDatabaseSet.size() > 0) {
        // As the call to close will modify the original set, we must take a copy to iterate over.
        DatabaseSet openSetCopy;
        openSetCopy.swap(m_openDatabaseSet);
        DatabaseSet::iterator end = openSetCopy.end();
        for (DatabaseSet::iterator it = openSetCopy.begin(); it != end; ++it)
           (*it)->close();
    }

    // Detach the thread so its resources are no longer of any concern to anyone else
    detachThread(m_threadID);

    DatabaseTaskSynchronizer* cleanupSync = m_cleanupSync;
    
    // Clear the self refptr, possibly resulting in deletion
    m_selfRef = 0;

    if (cleanupSync) // Someone wanted to know when we were done cleaning up.
        cleanupSync->taskCompleted();

    return 0;
}

void DatabaseThread::recordDatabaseOpen(Database* database)
{
    ASSERT(currentThread() == m_threadID);
    ASSERT(database);
    ASSERT(!m_openDatabaseSet.contains(database));
    m_openDatabaseSet.add(database);
}

void DatabaseThread::recordDatabaseClosed(Database* database)
{
    ASSERT(currentThread() == m_threadID);
    ASSERT(database);
    ASSERT(m_queue.killed() || m_openDatabaseSet.contains(database));
    m_openDatabaseSet.remove(database);
}

void DatabaseThread::scheduleTask(PassOwnPtr<DatabaseTask> task)
{
    m_queue.append(task);
}

void DatabaseThread::scheduleImmediateTask(PassOwnPtr<DatabaseTask> task)
{
    m_queue.prepend(task);
}

class SameDatabasePredicate {
public:
    SameDatabasePredicate(const Database* database) : m_database(database) { }
    bool operator()(DatabaseTask* task) const { return task->database() == m_database; }
private:
    const Database* m_database;
};

void DatabaseThread::unscheduleDatabaseTasks(Database* database)
{
    // Note that the thread loop is running, so some tasks for the database
    // may still be executed. This is unavoidable.
    SameDatabasePredicate predicate(database);
    m_queue.removeIf(predicate);
}
} // namespace WebCore
#endif
