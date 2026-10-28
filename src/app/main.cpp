#include "Editor.h"

#include <QCommandLineParser>
#include <QGuiApplication>
#include <QQmlAbstractUrlInterceptor>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSurfaceFormat>
#include <QTimer>
#include <QUrl>

#include <cstdio>

namespace {

// The UI only ever loads bundled or local files; anything else is refused.
class OfflineInterceptor : public QQmlAbstractUrlInterceptor {
public:
    QUrl intercept(const QUrl& url, DataType) override
    {
        const QString s = url.scheme();
        if (s.isEmpty() || s == QLatin1String("qrc") || s == QLatin1String("file") || s == QLatin1String("data"))
            return url;
        qWarning("Blocked network URL: %s", qPrintable(url.toString()));
        return QUrl();
    }
};

} // namespace

int main(int argc, char* argv[])
{
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    QSurfaceFormat fmt;
    fmt.setStencilBufferSize(8);
    fmt.setDepthBufferSize(24);
    fmt.setSwapInterval(1); // vsync; the render loop runs at the display's refresh rate
    QSurfaceFormat::setDefaultFormat(fmt);

    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("drav"));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("drav"));
    QGuiApplication::setOrganizationName(QStringLiteral("drav"));
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QCommandLineParser cli;
    cli.setApplicationDescription(QStringLiteral("Offline vector design tool"));
    cli.addHelpOption();
    cli.addPositionalArgument(QStringLiteral("file"), QStringLiteral("Document or SVG/image to open."));
    QCommandLineOption demo(QStringLiteral("demo"), QStringLiteral("Open the showcase document."));
    QCommandLineOption shot(QStringLiteral("screenshot"), QStringLiteral("Save a window screenshot and quit."), QStringLiteral("png"));
    QCommandLineOption bench(QStringLiteral("bench"), QStringLiteral("Pan/zoom benchmark for N seconds, print FPS, quit."), QStringLiteral("seconds"));
    cli.addOptions({demo, shot, bench});
    cli.process(app);

    drav::Editor editor;
    QQmlApplicationEngine engine;
    engine.addUrlInterceptor(new OfflineInterceptor);
    engine.rootContext()->setContextProperty(QStringLiteral("editor"), &editor);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); },
        Qt::QueuedConnection);
    engine.loadFromModule("Drav", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;

    // Let the window lay out before fitting the view.
    QTimer::singleShot(0, &editor, [&] {
        if (!cli.positionalArguments().isEmpty())
            editor.open(QUrl::fromLocalFile(cli.positionalArguments().first()));
        else if (cli.isSet(demo) || cli.isSet(shot) || cli.isSet(bench))
            editor.loadDemo();
        else
            editor.newDocument();
    });

    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    if (cli.isSet(shot) && window) {
        const QString path = cli.value(shot);
        QTimer::singleShot(2000, &app, [window, path] {
            window->grabWindow().save(path);
            QCoreApplication::quit();
        });
    }
    if (cli.isSet(bench) && window) {
        const int seconds = std::max(1, cli.value(bench).toInt());
        auto* timer = new QTimer(&app);
        auto* elapsed = new QElapsedTimer;
        auto* frames = new int(0);
        // Each rendered frame triggers the next pan step, so this measures the full pipeline.
        QObject::connect(window, &QQuickWindow::frameSwapped, &app, [&editor, elapsed, frames, seconds] {
            if (!elapsed->isValid())
                elapsed->start();
            ++*frames;
            const double t = elapsed->elapsed() / 1000.0;
            editor.panBy(QPointF(std::sin(t * 3) * 6, std::cos(t * 2) * 4));
            if (*frames % 60 == 0)
                editor.zoomAt(QPointF(400, 300), (*frames / 60) % 2 ? 1.25 : 0.8);
            if (t >= seconds) {
                std::printf("bench: %d frames in %.2fs = %.1f fps\n", *frames, t, *frames / t);
                std::fflush(stdout);
                QCoreApplication::quit();
            }
        });
        QObject::connect(timer, &QTimer::timeout, &editor, [&editor] { editor.requestUpdate(); });
        timer->start(1500);
    }
    return app.exec();
}
