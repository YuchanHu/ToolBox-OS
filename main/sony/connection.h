#pragma once
#include "automation.h"
#include "body.h"
#include "capture.h"
#include "media.h"

#include <string>
#include <string_view>
#include <vector>

namespace sony {
enum class ConnectionPhase { Idle, Scanning, ScanDone, Phone, Connecting, Verify, Ready, Failed };
struct ConnectionState {
    ConnectionPhase phase = ConnectionPhase::Idle;
    unsigned revision = 0;
    std::vector<std::string> cameras;
    std::string ssid, ap, url, fingerprint, model, message;
    bool fingerprintChanged = false;
    CaptureState capture;
    BodyState body;
    bool mediaAvailable = false;
    bool mediaDeleteAvailable = false;
    MediaState media;
    AutomationState automation;
};

struct CameraCredentials {
    std::string ssid, wifiPassword, user, sshPassword, fingerprint;
    bool valid() const;
    void clear();
    ~CameraCredentials();
};
bool validCredential(unsigned field, std::string_view value);
bool readProvisionForm(std::string_view form, CameraCredentials& out);

class Connection {
public:
    static Connection& instance();
    bool hasSaved() const;
    ConnectionState state() const;
    unsigned revision() const;
    bool scan();
    bool phone();
    bool connect(CameraCredentials credentials);
    bool connectSaved();
    bool retry();
    void confirmFingerprint(std::string_view shown);
    bool capture(CaptureRequest request);
    void refreshParameters();
    bool body(BodyCommand command);
    void clearBody();
    void prepareMedia();
    bool openMedia(bool refresh = false);
    void closeMedia(bool keepWarm = false);
    void showMedia(MediaWindow);
    bool deleteMedia(std::shared_ptr<const MediaItem>);
    bool automate(AutoAction, IntervalPlan = {}, std::shared_ptr<const Preset> expected = {});
    void cancelAutomation();
    void stop();
private:
    bool launch(ConnectionPhase, CameraCredentials credentials = {});
};

struct Feedback { bool sound = true, vibration = true; };
Feedback loadFeedback();
void saveFeedback(Feedback value);
}
