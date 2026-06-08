#pragma once
//
// IService — uniform lifecycle for every backend service.  Concrete services
// inherit QObject + IService, are moved onto their own QThread by the
// AppController, and communicate only through the EventBus.
//
#include <QObject>
#include <QThread>

namespace polymath {

class IService {
public:
    virtual ~IService() = default;
    virtual void start() = 0;             // called on the service's own thread
    virtual void stop() = 0;
    virtual const char* serviceName() const = 0;
};

// Moves `obj` onto a fresh QThread and invokes start() once the thread runs.
// Returns the thread (owned by caller; quit()+wait() on shutdown).
inline QThread* runOnThread(QObject* obj, IService* svc) {
    auto* thread = new QThread();
    obj->moveToThread(thread);
    QObject::connect(thread, &QThread::started, obj, [svc] { svc->start(); });
    QObject::connect(thread, &QThread::finished, obj, [svc] { svc->stop(); });
    thread->start();
    return thread;
}

} // namespace polymath
