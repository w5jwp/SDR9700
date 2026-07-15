#pragma once

#include <QHash>
#include <QString>
#include <QVariant>

class AppSettings
{
  public:
    static AppSettings& instance();

    QVariant value(const QString& key, const QVariant& defaultValue = QVariant()) const;
    bool setValue(const QString& key, const QVariant& settingValue);
    bool contains(const QString& key) const;
    bool remove(const QString& key);
    bool save() const;
    static QString configPath();

  private:
    AppSettings();

    void load();
    bool loadJson(const QString& path);
    static QString encodeValue(const QVariant& value);

    QHash<QString, QString> m_values;
};
