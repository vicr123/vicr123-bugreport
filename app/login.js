var img1 = new Image();
img1.src = "/images/loader.svg";

$(function() {
    $("body").i18n();
    $("#login").show();
    $("#register").hide();
    $("#2fa").hide();

    $("#login input").keypress(function(e) {
        if (e.keyCode == 13) {
            attemptLogin();
            return false;
        }
        return true;
    });

    $("#2fa input").keypress(function(e) {
        if (e.keyCode == 13) {
            attemptTotpLogin();
            return false;
        }
        return true;
    });

    $("#register input").keypress(function(e) {
        if (e.keyCode == 13) {
            attemptRegister();
            return false;
        }
        return true;
    });
});

function retranslatePage() {
    $("#username").attr("placeholder", $.i18n("username"));
    $("#password").attr("placeholder", $.i18n("password"));
    $("#regemail").attr("placeholder", $.i18n("email"));
    $("#regusername").attr("placeholder", $.i18n("username"));
    $("#regpassword").attr("placeholder", $.i18n("password"));
    $("#regpasswordconfirm").attr("placeholder", $.i18n("confirm-password"));
    $("#totpCode").attr("placeholder", $.i18n("2fa-code"));
}

function attemptLogin() {
    $("#coverLoader").addClass("show");
    $("#error").removeClass("show");
    let username = $("#username").val();
    let password = $("#password").val();
    
    if (username == "" || password == "") {
        $("#error").addClass("show");
        $("#coverLoader").removeClass("show");
        parent.resizeLoginScreen();
    } else {
        //All good, form the JSON and login
        let registerPayload = {
            username: username,
            password: password
        }

        $.ajax({
            contentType: "application/json",
            timeout: 10000,
            type: "POST",
            url: "/api/users/getToken",
            success: function(data, status, jqXHR) {
                localStorage.setItem("token", data.token);
                parent.reloadLoginState();
                parent.toast("Logged In", "Logged in as " + username);
                resetView();
            },
            error: function(jxXHR, status, err) {
                if (jxXHR.responseText == "TOTP Token Required") {
                    $("#login").hide();
                    $("#2fa").show();
                    $("#totpCode").focus();
                } else {
                    $("#error").addClass("show");
                }
                    parent.resizeLoginScreen();
                $("#coverLoader").removeClass("show");
            },
            data: JSON.stringify(registerPayload) + "\r\n\r\n"
        });
    }
}

function attemptTotpLogin() {
    $("#coverLoader").addClass("show");
    $("#totpError").removeClass("show");
    let username = $("#username").val();
    let password = $("#password").val();
    let totpCode = $("#totpCode").val();
    
    if (totpCode == "") {
        $("#totpError").addClass("show");
        $("#coverLoader").removeClass("show");
        parent.resizeLoginScreen();
    } else {
        //All good, form the JSON and login
        let registerPayload = {
            username: username,
            password: password,
            totpCode: totpCode
        }


        $.ajax({
            contentType: "application/json",
            timeout: 10000,
            type: "POST",
            url: "/api/users/getToken",
            success: function(data, status, jqXHR) {
                localStorage.setItem("token", data.token);
                parent.reloadLoginState();
                parent.toast("Logged In", "Logged in as " + username);
                resetView();
            },
            error: function(jxXHR, status, err) {
                $("#totpError").addClass("show");
                $("#coverLoader").removeClass("show");
                parent.resizeLoginScreen();
            },
            data: JSON.stringify(registerPayload) + "\r\n\r\n"
        });
    }
}

function register() {
    $("#login").hide();
    $("#register").show();
    parent.resizeLoginScreen();
}

function resetView() {
    parent.dismissDialog('dlgLogin');

    $("#login").show();
    $("#register").hide();
    $("#2fa").hide();
    $("#username").val("");
    $("#password").val("");
    $("#totpCode").val("");
    $("#error").removeClass("show");
    $("#regPasswordError").removeClass("show");
    $("#regFieldError").removeClass("show");
    $("#regpassword").val("");
    $("#regpasswordconfirm").val("");
    $("#regemail").val("");
    $("#regusername").val("");
    $("#regUsernameError").removeClass("show");
    $("#regEmailError").removeClass("show");
    $("#regEmailValidityError").removeClass("show");
    $("#regServerError").removeClass("show");
    $("#coverLoader").removeClass("show");
    $("#regTerms").removeClass("show");
    
    parent.resizeLoginScreen();
}

function attemptRegister() {
    $("#coverLoader").addClass("show");
    $("#regPasswordError").removeClass("show");
    $("#regFieldError").removeClass("show");
    $("#regUsernameError").removeClass("show");
    $("#regEmailError").removeClass("show");
    $("#regEmailValidityError").removeClass("show");
    $("#regServerError").removeClass("show");
    $("#regTerms").removeClass("show");

    //Check terms
    if (!$("#terms").prop("checked")) {
        $("#regTerms").addClass("show");
        $("#coverLoader").removeClass("show");
        parent.resizeLoginScreen();
        return;
    }

    //Check passwords
    if ($("#regpassword").val() != $("#regpasswordconfirm").val()) {
        $("#regPasswordError").addClass("show");
        $("#coverLoader").removeClass("show");
        parent.resizeLoginScreen();
        return;
    }

    //Check fields
    let email = $("#regemail").val();
    let username = $("#regusername").val();
    let password = $("#regpassword").val();

    if (email == "" || username == "" || password == "") {
        $("#regFieldError").addClass("show");
        $("#coverLoader").removeClass("show");
        parent.resizeLoginScreen();
        return;
    }

    //All good, form the JSON and register
    let registerPayload = {
        email: email,
        username: username,
        password: password
    }


    $.ajax({
        contentType: "application/json",
        timeout: 10000,
        type: "POST",
        url: "/api/users/create",
        success: function(data, status, jqXHR) {
            localStorage.setItem("token", data.token);
            parent.reloadLoginState();
            resetView();
        },
        error: function(jxXHR, status, err) {
            if (jxXHR.status == 409) {
                if (jxXHR.responseText == "Email Already Used") {
                    $("#regEmailError").addClass("show");
                } else {
                    $("#regUsernameError").addClass("show");
                }
            } else if (jxXHR.status == 500) {
                $("#regServerError").addClass("show");
            } else if (jxXHR.status == 400) {
                $("#regEmailValidityError").addClass("show");
            }
            $("#coverLoader").removeClass("show");
            parent.resizeLoginScreen();
        },
        data: JSON.stringify(registerPayload) + "\r\n\r\n"
    });
}

function renderGoogleButton() {
    gapi.signin2.render("my-signin2", {
        scope: "profile email",
        width: 100,
        height: 30
    });
}

function openTerms() {
    $("#coverLoader").addClass("show");
    $.ajax({
        contentType: "text/plain",
        timeout: 10000,
        type: "GET",
        url: "/app/terms.html",
        success: function(data, status, jqXHR) {
            $("#termsBox").html(data);
            $("#register").hide();
            $("#terms").show();
            $("#coverLoader").removeClass("show");
        },
        error: function(jxXHR, status, err) {
            $("#termsBox").text("An error occurred retrieving the Terms and Conditions.");
            $("#register").hide();
            $("#terms").show();
            $("#coverLoader").removeClass("show");
        }
    });
}
