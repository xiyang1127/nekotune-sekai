#pragma once
#include <nekotune/sdk/extension_context.h>
#include <QDir>
#include <QHash>
#include <QList>
#include <map>
#include <optional>

/// Synchronous in-memory extension context for plugin tests.
class FakeContext final : public nekotune::sdk::IExtensionContext {
  public:
    struct Event {
        QString name;
        nekotune::sdk::Params data;
    };
    QString extensionId = "test.extension";
    QString dataPath = QDir::tempPath();
    nekotune::sdk::Params configuration;
    QHash<QString, QString> secrets;
    QHash<QString, QVariant> storage;
    std::function<nekotune::Result<void>(const QString &key, const QString &value)> secretWriter;
    std::function<void(const QString &, const nekotune::sdk::Params &, nekotune::sdk::Reply)> host =
        [](const QString &method, const nekotune::sdk::Params &params, nekotune::sdk::Reply done) {
            done(nekotune::sdk::Params{{"method", method}, {"params", params}, {"task", "fixture-task"}});
        };
    QHash<QString, nekotune::sdk::Handler> services, commands;
    QHash<QString, nekotune::sdk::IMusicProvider *> music;
    QHash<QString, nekotune::sdk::ILyricsProvider *> lyrics;
    QHash<QString, nekotune::sdk::Params> descriptors;
    std::multimap<QString, std::pair<int, nekotune::sdk::EventHandler>> listeners;
    QList<Event> events;
    QStringList logs;

    /// Deliver a host event to subscribers, like the runtime does.
    void deliver(const QString &name, const nekotune::sdk::Params &event) {
        auto copy = listeners;
        for (const auto &[key, value] : copy)
            if (key == name || key == "*")
                value.second(event);
    }
    bool published(const QString &name) const {
        for (const auto &event : events)
            if (event.name == name)
                return true;
        return false;
    }
    nekotune::Result<nekotune::sdk::Params> call(const QString &service, const nekotune::sdk::Params &params = {}) {
        std::optional<nekotune::Result<nekotune::sdk::Params>> result;
        const auto handler = services.value(service);
        if (!handler)
            return nekotune::failure("missing service " + service);
        handler(params, [&](nekotune::Result<nekotune::sdk::Params> value) { result = std::move(value); });
        return result ? *result : nekotune::Result<nekotune::sdk::Params>(nekotune::failure("pending"));
    }

    QString id() const override { return extensionId; }
    QString version() const override { return "1.0.0"; }
    QString directory() const override { return dataPath; }
    QString dataDirectory() const override { return dataPath; }
    nekotune::sdk::Params config() const override { return configuration; }
    void updateConfig(const nekotune::sdk::Params &config, std::function<void(nekotune::Result<void>)> done) override {
        configuration = config;
        deliver("extension.config_changed", {{"event", "extension.config_changed"}, {"config", config}});
        done({});
    }
    void hostCall(const QString &method, const nekotune::sdk::Params &params, nekotune::sdk::Reply done) override {
        host(method, params, std::move(done));
    }
    nekotune::sdk::RegistrationPtr registerService(const QString &id, nekotune::sdk::Handler handler) override {
        services.insert(id, std::move(handler));
        return handle([this, id] { services.remove(id); });
    }
    void callService(const QString &, const QString &, const nekotune::sdk::Params &, nekotune::sdk::Reply done) override {
        done(nekotune::failure("unsupported"));
    }
    nekotune::sdk::RegistrationPtr registerCommand(const QString &id, nekotune::sdk::Handler handler,
                                                   const nekotune::sdk::Params &) override {
        commands.insert(id, std::move(handler));
        return handle([this, id] { commands.remove(id); });
    }
    nekotune::sdk::RegistrationPtr registerUi(const QString &, const QString &, const nekotune::sdk::Params &) override {
        return handle([] {});
    }
    nekotune::sdk::RegistrationPtr registerMusicProvider(const QString &id, nekotune::sdk::IMusicProvider *provider,
                                                         const nekotune::sdk::Params &descriptor) override {
        if (music.contains(id))
            qFatal("Duplicate music registration");
        music.insert(id, provider);
        descriptors.insert("music/" + id, descriptor);
        return handle([this, id] { music.remove(id); });
    }
    nekotune::sdk::RegistrationPtr registerLyricsProvider(const QString &id, nekotune::sdk::ILyricsProvider *provider,
                                                          const nekotune::sdk::Params &descriptor) override {
        if (lyrics.contains(id))
            qFatal("Duplicate lyrics registration");
        lyrics.insert(id, provider);
        descriptors.insert("lyrics/" + id, descriptor);
        return handle([this, id] { lyrics.remove(id); });
    }
    nekotune::sdk::RegistrationPtr on(const QString &event, nekotune::sdk::EventHandler handler) override {
        const int key = ++m_next;
        listeners.insert({event, {key, std::move(handler)}});
        return handle([this, key] {
            std::erase_if(listeners, [key](const auto &entry) { return entry.second.first == key; });
        });
    }
    void publish(const QString &event, const nekotune::sdk::Params &data) override { events.append({event, data}); }
    void storageGet(const QString &key, std::function<void(nekotune::Result<QVariant>)> done) override {
        done(storage.value(key));
    }
    void storageSet(const QString &key, const QVariant &value, std::function<void(nekotune::Result<void>)> done) override {
        storage.insert(key, value);
        done({});
    }
    void secretGet(const QString &key, std::function<void(nekotune::Result<QString>)> done) override {
        done(secrets.value(key));
    }
    void secretSet(const QString &key, const QString &value, std::function<void(nekotune::Result<void>)> done) override {
        if (secretWriter)
            if (auto written = secretWriter(key, value); !written)
                return done(written);
        secrets.insert(key, value);
        done({});
    }
    void secretDelete(const QString &key, std::function<void(nekotune::Result<void>)> done) override {
        secrets.remove(key);
        done({});
    }
    void reportTask(const QString &, const nekotune::sdk::Params &) override {}
    void log(QtMsgType, const QString &message) override { logs.append(message); }

  private:
    class Handle final : public nekotune::sdk::Registration {
      public:
        explicit Handle(std::function<void()> remove) : m_remove(std::move(remove)) {}
        ~Handle() override { dispose(); }
        void dispose() override {
            if (auto remove = std::exchange(m_remove, {}))
                remove();
        }

      private:
        std::function<void()> m_remove;
    };
    nekotune::sdk::RegistrationPtr handle(std::function<void()> remove) { return std::make_unique<Handle>(std::move(remove)); }
    int m_next = 0;
};
