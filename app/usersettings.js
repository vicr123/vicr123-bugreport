var img1 = new Image();
img1.src = "/images/loader.svg";

$(function() {
    $("#settingsChange").show();

    $("#settingsChange input").keypress(function(e) {
        if (e.keyCode == 13) {
            changeSettings();
            return false;
        }
        return true;
    });
});

function changeSettings() {
    $("#coverLoader").addClass("show");
    $("#error").removeClass("show");
    $("#passwordError").removeClass("show");
    let username = $("#username").val();
    let password = $("#password").val();
    
    //Check passwords
    if ($("#password").val() != $("#passwordConfirm").val()) {
        $("#passwordError").addClass("show");
        $("#coverLoader").removeClass("show");
        parent.resizeLoginScreen();
        return;
    }

    //All good, form the JSON and login
    let editPayload = {};

    if ($("#username").val() != "") {
        editPayload.username = $("#username").val();
    }
    if ($("#password").val() != "") {
        editPayload.password = $("#password").val();
    }

    $.ajax({
        contentType: "application/json",
        timeout: 10000,
        type: "PATCH",
        url: "/api/users/me",
        success: function(data, status, jqXHR) {
            parent.toast("Profile Settings Changed", "Your profile settings have been changed.");
            parent.reloadLoginState();
            resetView();
        },
        error: function(jxXHR, status, err) {
            $("#error").addClass("show");
            parent.resizeLoginScreen();
            $("#coverLoader").removeClass("show");
        },
        beforeSend: function (xhr) {
            xhr.setRequestHeader("Authorization", localStorage.getItem("token"));
            xhr.setRequestHeader("Password", $("#currentPassword").val());
        },
        data: JSON.stringify(editPayload) + "\r\n\r\n"
    });
}
function resetView() {
    parent.dismissDialog('dlgProfileSettings');

    $("#login").show();
    $("#username").val("");
    $("#password").val("");
    $("#passwordConfirm").val("");
    $("#currentPassword").val("");
    $("#error").removeClass("show");
    $("#coverLoader").removeClass("show");
    
    parent.resizeLoginScreen();
}