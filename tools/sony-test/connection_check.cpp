#include "sony/connection.h"
#include "sony/keypad.h"
#include <cstdio>
#include <cstdlib>
#include <set>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)

int main() {
    using sony::Keypad;
    Keypad pad;
    pad.reset({}, 3);
    pad.tap(0, 0);
    pad.tap(0, 200);
    CHECK(pad.display() == "b");
    CHECK(!pad.tick(1049));
    CHECK(pad.tick(1050) && pad.value() == "b");
    pad.tap(0, 1200);
    pad.setMode(Keypad::Mode::Upper);
    pad.tap(1, 1300);
    pad.commit();
    CHECK(pad.value() == "baD");
    pad.tap(0, 1400);
    CHECK(pad.display() == "baD");
    pad.erase();
    pad.tap(8, 1500);
    CHECK(pad.value() == "ba ");
    for (auto mode : {Keypad::Mode::Lower, Keypad::Mode::Upper}) {
        pad.setMode(mode);
        unsigned spaces = 0;
        for (unsigned i = 0; i < 9; ++i) spaces += pad.key(i) == " ";
        CHECK(spaces == 1);
    }
    std::set<char> symbols;
    pad.setMode(Keypad::Mode::Symbol);
    for (unsigned page = 0; page < 4; ++page) {
        unsigned spaces = 0;
        for (unsigned i = 0; i < 9; ++i) {
            auto key = pad.key(i);
            if (!key.empty()) { CHECK(symbols.insert(key[0]).second); spaces += key == " "; }
        }
        CHECK(spaces <= 1);
        pad.extra();
    }
    CHECK(symbols.size() == 33 && symbols.count(' '));
    CHECK(pad.key(0) == "!");
    pad.reset({}, 64);
    pad.setMode(Keypad::Mode::Number);
    pad.tap(8, 0); pad.extra();
    CHECK(pad.value() == "90");

    sony::CameraCredentials credentials;
    CHECK(sony::readProvisionForm("ssid=DIRECT-%CE%B1&user=a%2Bb&wifi=aabbcc%21%21&password=p%25%26%3D+q", credentials));
    CHECK(credentials.ssid == "DIRECT-\xce\xb1" && credentials.user == "a+b" &&
          credentials.wifiPassword == "aabbcc!!" && credentials.sshPassword == "p%&= q");
    CHECK(!sony::readProvisionForm("ssid=DIRECT-X&ssid=x&wifi=12345678&user=u&password=p", credentials));
    CHECK(!sony::readProvisionForm("ssid=DIRECT-X&wifi=12345678&user=%00&password=p", credentials));
    CHECK(!sony::readProvisionForm("ssid=DIRECT-X&wifi=12345678&user=u&password=%GG", credentials));
    CHECK(!sony::validCredential(1, std::string(64, 'g')));
    CHECK(sony::validCredential(1, std::string(64, 'f')));
    CHECK(sony::validCredential(3, std::string(256, '?')));
    CHECK(!sony::validCredential(3, std::string(257, '?')));
    std::puts("Sony provisioning: multi-tap, timeout, modes, printable ASCII, form decoding and bounds passed");
}
