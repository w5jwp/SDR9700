#pragma once

#include <QString>
#include <QtGlobal>
#include <optional>

namespace sdr9700
{
// The IC-9700 identifies each RS-BA1 UDP transport by the socket endpoints and
// a pair of opaque session IDs. After a process crash, the replacement must
// reproduce all four values long enough to send the predecessor's departure;
// an authentication token alone cannot release a retained CI-V/audio pipe.
struct RadioSessionTransportIdentity
{
    quint16 localPort{0};
    quint16 remotePort{0};
    quint32 localSessionId{0};
    quint32 remoteSessionId{0};

    [[nodiscard]] bool isComplete() const
    {
        return localPort != 0 && remotePort != 0 && localSessionId != 0 && remoteSessionId != 0;
    }
};

struct RadioSessionRecoveryRecord
{
    // This file is a crash-recovery journal, not user configuration or a
    // second source of radio state. A normal authenticated shutdown removes
    // the record after the radio acknowledges token removal.
    QString radioAddress;
    QString ownerName;
    quint16 tokenRequest{0};
    quint32 token{0};
    RadioSessionTransportIdentity control;
    RadioSessionTransportIdentity civ;
    RadioSessionTransportIdentity audio;

    [[nodiscard]] bool hasTransportIdentities() const
    {
        return control.isComplete() && civ.isComplete() && audio.isComplete();
    }
};

class RadioSessionRecoveryStore
{
  public:
    [[nodiscard]] static bool save(const RadioSessionRecoveryRecord& record);
    [[nodiscard]] static std::optional<RadioSessionRecoveryRecord> load(const QString& radioAddress,
                                                                        const QString& ownerName);
    [[nodiscard]] static std::optional<RadioSessionRecoveryRecord> loadForRadio(const QString& radioAddress);
    static void removeOwned(const QString& radioAddress, const QString& ownerName);

  private:
    [[nodiscard]] static QString filePath();
};
} // namespace sdr9700
