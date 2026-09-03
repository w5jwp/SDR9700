#pragma once

#include <QHash>
#include <QString>
#include <QTimer>
#include <QVariant>

class AppSettings
{
  public:
    static AppSettings& instance();

    QVariant value(const QString& key, const QVariant& defaultValue = QVariant()) const;
    bool setValue(const QString& key, const QVariant& settingValue);
    void setValueDeferred(const QString& key, const QVariant& settingValue);
    bool contains(const QString& key) const;
    bool remove(const QString& key);
    bool save();
    static QString configPath();

  private:
    AppSettings();
    ~AppSettings();

    void load();
    bool loadJson(const QString& path);
    bool writeFile() const;
    static QString encodeValue(const QVariant& value);

    QHash<QString, QString> m_values;
    QTimer m_deferredSaveTimer;
    bool m_deferredSavePending{false};
    bool m_writesBlocked{false};
};
