// Declarations shared by the fake framework and the host test driver. The vtable layout (34 slots, onError 21,
// onInit 22, onFinalize 23) must match what nbt_service_app.cpp patches.
#ifndef NBTSERVICE_FAKE_APPFRAMEWORK_HPP
#define NBTSERVICE_FAKE_APPFRAMEWORK_HPP

#include <stddef.h>

namespace bmw {
class IDispatcher;
class Bundle;

class Parcel {
public:
    explicit Parcel(bool value);
    ~Parcel();
    void readBool(bool& value) const;
private:
    unsigned char m_storage[32];
};

enum RHMIInterfaceVersion { RHMI_INTERFACE_VERSION_1 = 1 };

template <typename T> class BasicString {
public:
    BasicString(const T* s);
    BasicString(const BasicString& other);
    ~BasicString();
    const T* raw() const { return m_data; }
private:
    T* m_data;
};

class TextID {
public:
    explicit TextID(unsigned id);
    unsigned id() const { return m_id; }
private:
    unsigned m_id;
    unsigned m_type;
};

class Runnable {
public:
    virtual ~Runnable();
    virtual void run() = 0;
};

template <typename R, typename A1, typename A2, typename A3, typename A4> struct Delegate {
    void* object;
    R (*stub)(void* object, A1 a1);
};
typedef Delegate<bool, const Parcel&, void, void, void> ActionDelegate;

const size_t kPluginAppStorage = 0x200;

#define FAKE_SLOT(n) virtual void s##n() {}

class PluginApp {
public:
    PluginApp(IDispatcher& dispatcher, BasicString<char> name, unsigned major, unsigned minor,
              RHMIInterfaceVersion rhmi, const Bundle& params, const Bundle& config);
    virtual ~PluginApp();
    FAKE_SLOT(2)  FAKE_SLOT(3)  FAKE_SLOT(4)  FAKE_SLOT(5)  FAKE_SLOT(6)  FAKE_SLOT(7)  FAKE_SLOT(8)
    FAKE_SLOT(9)  FAKE_SLOT(10) FAKE_SLOT(11) FAKE_SLOT(12) FAKE_SLOT(13) FAKE_SLOT(14) FAKE_SLOT(15)
    FAKE_SLOT(16) FAKE_SLOT(17) FAKE_SLOT(18) FAKE_SLOT(19) FAKE_SLOT(20)
    virtual void onError(unsigned code, const BasicString<char>& text);
    virtual bool onInit();
    virtual void onFinalize();
    FAKE_SLOT(24) FAKE_SLOT(25) FAKE_SLOT(26) FAKE_SLOT(27) FAKE_SLOT(28) FAKE_SLOT(29) FAKE_SLOT(30)
    FAKE_SLOT(31) FAKE_SLOT(32) FAKE_SLOT(33)
    void addHandler(unsigned actionId, const ActionDelegate& handler);
    void addHandler(unsigned id, unsigned type, const ActionDelegate& handler);
    void post(Runnable* runnable, unsigned delayMs);
    void updateModel(unsigned modelId, const TextID& text);
    void updateModel(unsigned modelId, bool value);
private:
    // The plugin allocates kPluginAppStorage bytes and constructs this object into them, so the fake must stay
    // well under that (the vptr counts too).
    unsigned char m_pad[64];
};

// What the plugin did, for the test's assertions.
struct FakeState {
    enum { kMax = 16 };
    unsigned actionIds[kMax];
    ActionDelegate actions[kMax];
    int actionCount;
    unsigned notifyIds[kMax], notifyTypes[kMax];
    ActionDelegate notifies[kMax];
    int notifyCount;
    Runnable* posted[kMax];
    unsigned postDelays[kMax];
    int postCount;
    unsigned lastTextModel, lastTextId, lastBoolModel;
    bool lastBool;
    int textUpdates, boolUpdates;
};
extern FakeState g_fake;

} // namespace bmw
#endif
