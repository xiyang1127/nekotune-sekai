import QtQuick
import "translations.js" as Translations
QtObject {
    property string language: i18n.language
    function text(key, locale) {
        const messages = Translations.messages[locale] || Translations.messages.en
        return messages[key] || (key.indexOf("vocal_") === 0 ? key.slice(6) : key)
    }
}
