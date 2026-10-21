#include <QDir>
#include <QFile>
#include <QJSEngine>
#include <QtTest>

/// Every plugin UI ships matching Chinese and English message keys.
class TranslationsTest final : public QObject {
    Q_OBJECT
  private slots:
    void keysMatch_data();
    void keysMatch();
};

void TranslationsTest::keysMatch_data() {
    QTest::addColumn<QString>("file");
    for (const auto *plugin : {"sekai"})
        QTest::newRow(plugin) << QStringLiteral(NEKOTUNE_EXTENSIONS_ROOT "/%1/qml/translations.js").arg(plugin);
}
void TranslationsTest::keysMatch() {
    QFETCH(QString, file);
    QFile source(file);
    QVERIFY(source.open(QIODevice::ReadOnly));
    QJSEngine engine;
    const auto result = engine.evaluate(QString::fromUtf8(source.readAll()).replace(".pragma library", ""), file);
    QVERIFY2(!result.isError(), qPrintable(result.toString()));
    const auto messages = engine.globalObject().property("messages");
    const auto keys = [&](const char *language) {
        auto list = messages.property(language).toVariant().toMap().keys();
        list.sort();
        return list;
    };
    QVERIFY(!keys("zh").isEmpty());
    QCOMPARE(keys("zh"), keys("en"));
}

QTEST_GUILESS_MAIN(TranslationsTest)
#include "translations_test.moc"
