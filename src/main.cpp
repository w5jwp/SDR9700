#include <QtGlobal>
#if !defined(Q_OS_LINUX)
#error "SDR9700 is Linux-only; POSIX signal handling and IC-9700 LAN code are not portable."
#endif

#include <QApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QIcon>
#include <QLoggingCategory>
#include <QMutex>
#include <QMutexLocker>
#include <QSocketNotifier>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <unistd.h>

#include "models/RadioModel.h"
#include "gui/MainWindow.h"
#include "AppBuildConfig.h"
#include "AppInfo.h"

namespace
{
int signalPipe[2] = {-1, -1};
QMutex logOutputMutex;
std::unique_ptr<QFile> logFile;
bool consoleLogEnabled{false};

struct LoggingOptions
{
    bool logEnabled{false};
    bool logFileRequested{false};
    QString logCategories;
    QString logFilePath;
};

QString logLevelName(QtMsgType type)
{
    switch (type)
    {
    case QtDebugMsg:
        return QStringLiteral("DBG");
    case QtInfoMsg:
        return QStringLiteral("INF");
    case QtWarningMsg:
        return QStringLiteral("WRN");
    case QtCriticalMsg:
        return QStringLiteral("CRT");
    case QtFatalMsg:
        return QStringLiteral("FTL");
    }
    return QStringLiteral("LOG");
}

void consoleMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    const QString category = context.category ? QString::fromLatin1(context.category) : QStringLiteral("default");
    const QString line = QStringLiteral("%1 %2 [%3] %4")
                             .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz")),
                                  logLevelName(type), category, message);
    const QByteArray encoded = line.toLocal8Bit();

    QMutexLocker lock(&logOutputMutex);
    if (consoleLogEnabled)
    {
        std::fprintf(stderr, "%s\n", encoded.constData());
        std::fflush(stderr);
    }
    if (logFile && logFile->isOpen())
    {
        logFile->write(encoded);
        logFile->write("\n");
        logFile->flush();
    }

    if (type == QtFatalMsg)
    {
        std::abort();
    }
}

QString quietLoggingRules()
{
    return QStringLiteral("*.debug=false\n"
                          "*.info=false\n"
                          "*.warning=true\n"
                          "*.critical=true");
}

QStringList parseLogCategories(const QString& value)
{
    QStringList categories;
    for (QString category : value.split(QLatin1Char(','), Qt::SkipEmptyParts))
    {
        category = category.trimmed().toLower();
        if (!category.isEmpty())
        {
            categories.append(category);
        }
    }
    return categories;
}

QString loggingRulesForOptions(const LoggingOptions& options)
{
    if (!options.logEnabled)
    {
#if SDR9700_DEBUG_BUILD
        return QStringLiteral("*.debug=true\n"
                              "*.info=true\n"
                              "*.warning=true\n"
                              "*.critical=true");
#else
        return quietLoggingRules();
#endif
    }

    const QStringList requested = parseLogCategories(options.logCategories);
    if (requested.contains(QStringLiteral("all")))
    {
        return QStringLiteral("*.debug=true\n"
                              "*.info=true\n"
                              "*.warning=true\n"
                              "*.critical=true");
    }

    QStringList rules{quietLoggingRules()};
    for (const QString& category : requested)
    {
        rules.append(QStringLiteral("%1.debug=true").arg(category));
        rules.append(QStringLiteral("%1.info=true").arg(category));
    }
    return rules.join(QLatin1Char('\n'));
}

bool consumeOptionValue(const QStringList& arguments, int* index, QString* value)
{
    if (*index + 1 >= arguments.size())
    {
        return false;
    }
    *value = arguments.at(*index + 1);
    ++(*index);
    return true;
}

bool extractLoggingOptions(const QStringList& arguments, QStringList* parserArguments, LoggingOptions* options,
                           QString* error)
{
    parserArguments->clear();
    parserArguments->append(arguments.value(0));

    for (int index = 1; index < arguments.size(); ++index)
    {
        const QString arg = arguments.at(index);
        if (arg == QStringLiteral("--log"))
        {
            *error = QStringLiteral("--log requires categories; use --log=all or --log=<categories>");
            return false;
        }
        if (arg.startsWith(QStringLiteral("--log=")))
        {
            options->logEnabled = true;
            options->logCategories = arg.mid(QStringLiteral("--log=").size());
            if (options->logCategories.trimmed().isEmpty())
            {
                *error = QStringLiteral("--log requires categories; use --log=all or --log=<categories>");
                return false;
            }
            continue;
        }
        if (arg == QStringLiteral("--log-file"))
        {
            options->logFileRequested = true;
            if (!consumeOptionValue(arguments, &index, &options->logFilePath))
            {
                *error = QStringLiteral("--log-file requires a path");
                return false;
            }
            continue;
        }
        if (arg.startsWith(QStringLiteral("--log-file=")))
        {
            options->logFileRequested = true;
            options->logFilePath = arg.mid(QStringLiteral("--log-file=").size());
            continue;
        }
        parserArguments->append(arg);
    }
    return true;
}

bool openLogFile(const QString& path)
{
    if (path.isEmpty())
    {
        std::fprintf(stderr, "--log-file requires a non-empty path\n");
        return false;
    }

    auto file = std::make_unique<QFile>(path);
    if (!file->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
    {
        std::fprintf(stderr, "Could not open log file: %s\n", path.toLocal8Bit().constData());
        return false;
    }
    logFile = std::move(file);
    return true;
}

void handleUnixSignal(int)
{
    const char byte = 1;
    if (signalPipe[1] != -1)
    {
        write(signalPipe[1], &byte, sizeof(byte));
    }
}

void installUnixSignalHandlers()
{
    if (pipe(signalPipe) != 0)
    {
        return;
    }

    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = handleUnixSignal;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, nullptr);
    sigaction(SIGTERM, &action, nullptr);
}

void configureQtMultimediaEnvironment()
{
    if (!qEnvironmentVariableIsSet("QT_FFMPEG_DECODING_HW_DEVICE_TYPES"))
    {
        qputenv("QT_FFMPEG_DECODING_HW_DEVICE_TYPES", "");
    }
    if (!qEnvironmentVariableIsSet("QT_FFMPEG_ENCODING_HW_DEVICE_TYPES"))
    {
        qputenv("QT_FFMPEG_ENCODING_HW_DEVICE_TYPES", "");
    }
}
} // namespace

int main(int argc, char* argv[])
{
    configureQtMultimediaEnvironment();

    QLoggingCategory::setFilterRules(quietLoggingRules());
    qInstallMessageHandler(consoleMessageHandler);
    installUnixSignalHandlers();

    QApplication app(argc, argv);
    app.setApplicationName("SDR9700");
    app.setOrganizationName("SDR9700");
    app.setApplicationVersion(APP_VERSION);
    app.setDesktopFileName(QStringLiteral("sdr9700"));
    app.setWindowIcon(QIcon(QStringLiteral(":/images/icons/sdr9700_app_icon.png")));

    LoggingOptions loggingOptions;
    QStringList parserArguments;
    QString loggingOptionError;
    if (!extractLoggingOptions(QCoreApplication::arguments(), &parserArguments, &loggingOptions, &loggingOptionError))
    {
        std::fprintf(stderr, "%s\n", loggingOptionError.toLocal8Bit().constData());
        return 1;
    }

    QCommandLineParser parser;
    parser.setApplicationDescription("Icom IC-9700 Client");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption(QCommandLineOption(QStringLiteral("log"),
                                        QStringLiteral("Enable console logging; use --log=all or --log=<categories>."),
                                        QStringLiteral("categories")));
    parser.addOption(QCommandLineOption(QStringLiteral("log-file"),
                                        QStringLiteral("Append formatted console logs to <path>."),
                                        QStringLiteral("path")));
    parser.process(parserArguments);

    if (loggingOptions.logFileRequested && !openLogFile(loggingOptions.logFilePath))
    {
        return 1;
    }
    consoleLogEnabled = loggingOptions.logEnabled;
    QLoggingCategory::setFilterRules(loggingRulesForOptions(loggingOptions));

    app.setStyle("Fusion");
    QPalette dark;
    dark.setColor(QPalette::Window, QColor(30, 30, 30));
    dark.setColor(QPalette::WindowText, Qt::white);
    dark.setColor(QPalette::Base, QColor(20, 20, 20));
    dark.setColor(QPalette::AlternateBase, QColor(40, 40, 40));
    dark.setColor(QPalette::Text, Qt::white);
    dark.setColor(QPalette::Button, QColor(50, 50, 50));
    dark.setColor(QPalette::ButtonText, Qt::white);
    dark.setColor(QPalette::Highlight, QColor(42, 130, 218));
    dark.setColor(QPalette::HighlightedText, Qt::black);
    app.setPalette(dark);
    app.setStyleSheet(QStringLiteral("QComboBox { padding-left: 10px; padding-right: 24px; }"
                                     "QComboBox QAbstractItemView::item { padding: 4px 10px; }"));

    auto* model = new RadioModel;
    MainWindow win(model);
    // Transfer model ownership to win so the destructor order is always
    // win -> model, whether the app exits via closeEvent or signal kill.
    model->setParent(&win);

    if (signalPipe[0] != -1)
    {
        auto* signalNotifier = new QSocketNotifier(signalPipe[0], QSocketNotifier::Read, &win);
        // Route Unix signals through win.close() so closeEvent() fires:
        // saveWindowLayout() runs, disconnectFromRadio() is called cleanly,
        // and the last-window-closed signal causes exec() to return.
        QObject::connect(signalNotifier, &QSocketNotifier::activated, &win,
                         [&win](int fd)
                         {
                             char bytes[16];
                             read(fd, bytes, sizeof(bytes));
                             win.close();
                         });
    }
    win.show();

    return app.exec();
}
