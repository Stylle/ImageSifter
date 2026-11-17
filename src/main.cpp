#include "window.h"
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setApplicationName("ImageSifter");
    app.setOrganizationName("ImageSifter");
    app.setApplicationVersion(APP_VERSION);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"check-runtime", QObject::tr("检查部署所需的格式插件及 SQLite 驱动后退出")});
    parser.addPositionalArgument("project", QObject::tr("要打开的 .imagesifter 工程"));
    parser.process(app);
    if (parser.isSet("check-runtime")) {
        auto formats = QImageReader::supportedImageFormats();
        for (auto name : QList<QByteArray>{"jpeg", "png", "gif", "webp", "tiff"})
            if (!formats.contains(name))
                return 2;
        auto db = QSqlDatabase::addDatabase("QSQLITE", "runtime-check");
        db.setDatabaseName(":memory:");
        if (!db.open())
            return 3;
        db.close();
        db = QSqlDatabase();
        QSqlDatabase::removeDatabase("runtime-check");
        return 0;
    }
    app.setStyle("Fusion");
    app.setWindowIcon(QIcon(":/logo.svg"));
    QImageReader::setAllocationLimit(256);
    QFile style(":/theme.qss");
    if (style.open(QIODevice::ReadOnly))
        app.setStyleSheet(QString::fromUtf8(style.readAll()));
    QFont font("Segoe UI");
    font.setPointSize(10);
    app.setFont(font);
    Window window;
    if (!parser.positionalArguments().isEmpty())
        window.openProject(parser.positionalArguments().first());
    window.show();
    return app.exec();
}
