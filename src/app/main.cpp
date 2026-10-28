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
#include <QElapsedTimer>

#include <cmath>
#include <memory>
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
    QCommandLineOption select(QStringLiteral("select"), QStringLiteral("Select a layer by name (\"Name#2\" picks the second match)."), QStringLiteral("name"));
    QCommandLineOption show(QStringLiteral("show"), QStringLiteral("Open a named window or popup (for screenshots)."), QStringLiteral("objectName"));
    QCommandLineOption script(QStringLiteral("script"), QStringLiteral("Run a script file (see docs/scripting.md) after opening."), QStringLiteral("js"));
    cli.addOptions({demo, shot, bench, select, show, script});
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
        if (cli.isSet(select)) {
            QString name = cli.value(select);
            int skip = 0;
            if (int hash = name.lastIndexOf(QLatin1Char('#')); hash > 0) {
                skip = std::max(0, name.mid(hash + 1).toInt() - 1);
                name.truncate(hash);
            }
            drav::NodeId found = drav::kNoNode;
            editor.doc().walk(editor.doc().currentPage(), [&](const drav::Node& n) {
                if (!found && n.p.name == name && skip-- == 0)
                    found = n.id;
                return !found;
            });
            if (found)
                editor.select({found});
        }
        if (cli.isSet(script)) {
            const QString out = editor.runPlugin(cli.value(script));
            if (!out.isEmpty())
                qInfo().noquote() << out;
        }
    });

    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    QQuickWindow* shotWindow = window;
    if (cli.isSet(show) && window) {
        QObject* target = window->findChild<QObject*>(cli.value(show));
        if (auto* w = qobject_cast<QQuickWindow*>(target)) {
            QTimer::singleShot(500, w, [w] { w->show(); });
            shotWindow = w;
        } else if (target) {
            QTimer::singleShot(500, target, [target] { QMetaObject::invokeMethod(target, "open"); });
        }
    }
    if (cli.isSet(shot) && shotWindow) {
        const QString path = cli.value(shot);
        QTimer::singleShot(2000, &app, [window, shotWindow, path] {
            shotWindow->grabWindow().save(path);
            window->setProperty("forceClose", true); // no "save changes?" prompt in screenshot runs
            QCoreApplication::quit();
        });
    }
    if (cli.isSet(bench) && window) {
        // Each rendered frame triggers the next pan/zoom step, so this measures the whole pipeline
        // (snapshot, picture replay on the GPU, compositing) at the display's frame pacing.
        struct Bench {
            QElapsedTimer clock;
            int frames = 0;
            double seconds = 0;
        };
        auto bench_ = std::make_shared<Bench>();
        bench_->seconds = std::max(1, cli.value(bench).toInt());
        QObject::connect(window, &QQuickWindow::frameSwapped, &editor, [&editor, bench_] {
            Bench& b = *bench_;
            if (!b.clock.isValid())
                b.clock.start();
            ++b.frames;
            const double t = b.clock.elapsed() / 1000.0;
            editor.panBy(QPointF(std::sin(t * 3) * 6, std::cos(t * 2) * 4));
            if (b.frames % 60 == 0)
                editor.zoomAt(QPointF(400, 300), (b.frames / 60) % 2 ? 1.25 : 0.8);
            if (t >= b.seconds) {
                std::printf("bench: %d frames in %.2fs = %.1f fps\n", b.frames, t, b.frames / t);
                std::fflush(stdout);
                QCoreApplication::quit();
            }
        }, Qt::QueuedConnection);
    }
    return app.exec();
}
