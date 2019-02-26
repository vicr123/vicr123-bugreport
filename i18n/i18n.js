//Prepare i18n
$.i18n().load({
    "en": "/i18n/en.json",
    "vi": "/i18n/vi.json"
});

let locale = localStorage.getItem("locale");
if (locale != null || locale != "null") {
    $.i18n({
        locale: locale
    });
}


window.onload = function() {
    retranslate();
};

function retranslate() {
    $("body").i18n();
    retranslatePage();
}
