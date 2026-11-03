// Host stand-in for libAppFramework.A.so / libCoreFramework.A.so: a bmw::PluginApp with the same 34-slot vtable
// layout, plus the few calls NbtService makes (updateModel for a TextID and for a bool, the two addHandler forms,
// post, Parcel::readBool). It records what the plugin did so the test can assert on it.
//
// It is a copy of milestone-7/tests/rhmiapp_fake_appframework.cpp in spirit, not a shared file: that one is
// upstream's and has no updateModel, and nothing of ours changes files outside id5/.
#include "nbtservice_fake_appframework.hpp"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

namespace bmw {

Parcel::Parcel(bool value) { memset(m_storage, 0, sizeof(m_storage)); m_storage[0] = value ? 1 : 0; }
Parcel::~Parcel() {}
void Parcel::readBool(bool& value) const { value = m_storage[0] != 0; }

TextID::TextID(unsigned id) : m_id(id), m_type(0x28) {}

template <> BasicString<char>::BasicString(const char* s) : m_data(strdup(s ? s : "")) {}
template <> BasicString<char>::BasicString(const BasicString<char>& o) : m_data(strdup(o.m_data ? o.m_data : "")) {}
template <> BasicString<char>::~BasicString() { free(m_data); }

Runnable::~Runnable() {}

FakeState g_fake;

PluginApp::PluginApp(IDispatcher&, BasicString<char> n, unsigned major, unsigned minor, RHMIInterfaceVersion v,
                     const Bundle&, const Bundle&) {
    memset(m_pad, 0, sizeof(m_pad));
    printf("FAKE PluginApp(%s, %u, %u, %d)\n", n.raw() ? n.raw() : "?", major, minor, (int)v);
}
PluginApp::~PluginApp() { printf("FAKE ~PluginApp\n"); }

void PluginApp::addHandler(unsigned actionId, const ActionDelegate& h) {
    if (g_fake.actionCount < FakeState::kMax) {
        g_fake.actionIds[g_fake.actionCount] = actionId;
        g_fake.actions[g_fake.actionCount++] = h;
    }
    printf("FAKE addHandler(action %u)\n", actionId);
}

void PluginApp::addHandler(unsigned id, unsigned type, const ActionDelegate& h) {
    if (g_fake.notifyCount < FakeState::kMax) {
        g_fake.notifyIds[g_fake.notifyCount] = id;
        g_fake.notifyTypes[g_fake.notifyCount] = type;
        g_fake.notifies[g_fake.notifyCount++] = h;
    }
    printf("FAKE addHandler(state %u, type %u)\n", id, type);
}

void PluginApp::post(Runnable* r, unsigned delayMs) {
    if (g_fake.postCount < FakeState::kMax) {
        g_fake.posted[g_fake.postCount] = r;
        g_fake.postDelays[g_fake.postCount++] = delayMs;
    }
    printf("FAKE post(runnable %p, %u ms)\n", (void*)r, delayMs);
}

void PluginApp::updateModel(unsigned modelId, const TextID& text) {
    g_fake.lastTextModel = modelId;
    g_fake.lastTextId = text.id();
    g_fake.textUpdates++;
    printf("FAKE updateModel(%u, TextID %u)\n", modelId, text.id());
}

void PluginApp::updateModel(unsigned modelId, bool value) {
    g_fake.lastBoolModel = modelId;
    g_fake.lastBool = value;
    g_fake.boolUpdates++;
    printf("FAKE updateModel(%u, bool %d)\n", modelId, (int)value);
}

bool PluginApp::onInit() { printf("FAKE base onInit\n"); return true; }
void PluginApp::onFinalize() { printf("FAKE base onFinalize\n"); }
void PluginApp::onError(unsigned c, const BasicString<char>& t) {
    printf("FAKE base onError %u %s\n", c, t.raw() ? t.raw() : "?");
}

} // namespace bmw
