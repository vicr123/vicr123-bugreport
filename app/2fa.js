var img1 = new Image();
img1.src = "/images/loader.svg";

let key;
$(function() {
    $("#pwd").show();
    $("#key").hide();
    $("#done").hide();
    $("#management").hide();
    $("#disable").hide();

    $("#pwd input").keypress(function(e) {
        if (e.keyCode == 13) {
            prepare2faInfo();
            return false;
        }
        return true;
    });

    $("#key input").keypress(function(e) {
        if (e.keyCode == 13) {
            attemptEnable2FA();
            return false;
        }
        return true;
    });

    $("#disable input").keypress(function(e) {
        if (e.keyCode == 13) {
            attemptDisable2FA();
            return false;
        }
        return true;
    });
});

function prepare2faInfo() {
    $("#coverLoader").addClass("show");
    $("#error").removeClass("show");
    let password = $("#password").val();
    
    if (password == "") {
        $("#error").addClass("show");
        $("#coverLoader").removeClass("show");
        parent.resizeLoginScreen();
    } else {
        //All good, form the JSON and login

        $.ajax({
            contentType: "application/json",
            timeout: 10000,
            type: "GET",
            url: "/api/users/me/2fa",
            success: function(data, status, jqXHR) {
                $("#pwd").hide();
                if (data.setupRequired) {
                    $("#key").show();

                    //Generate 2FA things
                    key = crypto.getRandomValues(new Uint8Array(10));
                    let base32Code = Unibabel.bufferToBase32(key).replace(/=/g, "");
                    let spacedCode = base32Code.replace(/(\w{4})/g, "$1 ").trim();
                    $("#qrcode").qrcode({
                        width: 128,
                        height: 128,
                        text: "otpauth://totp/vicr123-bug-reporting?issuer=vicr123%20Bug%20Reporting&secret=" + base32Code
                    });
                    $("#qrcode-code").html(spacedCode);
                    $("#codeInput").focus();
                } else {
                    $("#management").show();
                }
                $("#coverLoader").removeClass("show");
            },
            error: function(jxXHR, status, err) {
                $("#error").addClass("show");
                $("#coverLoader").removeClass("show");
                parent.resizeLoginScreen();
            },
            beforeSend: function (xhr) {
                xhr.setRequestHeader("Authorization", localStorage.getItem("token"));
                xhr.setRequestHeader("Password", password);

            }
        });
    }
}

function resetView() {
    parent.dismissDialog('dlg2fa');

    $("#pwd").show();
    $("#key").hide();
    $("#done").hide();
    $("#management").hide();
    $("#disable").hide();
    $("#codeError").hide();
    $("#error").hide();
    $("#password").val("");
    $("#codeInput").val("");
    $("#qrcode").empty();
    
    parent.resizeLoginScreen();
}

function attemptEnable2FA() {
    $("#coverLoader").addClass("show");
    $("#codeError").removeClass("show");
    let password = $("#password").val();
    let code = $("#codeInput").val();
    
    if (code == "") {
        $("#codeError").addClass("show");
        $("#coverLoader").removeClass("show");
        parent.resizeLoginScreen();
    } else {
        //All good, form the JSON and attempt to enable 2FA
        let codePayload = {
            key: Array.from(key),
            code: code
        }

        $.ajax({
            contentType: "application/json",
            timeout: 10000,
            type: "POST",
            url: "/api/users/me/2fa",
            success: function(data, status, jqXHR) {
                $("#key").hide();
                $("#done").show();

                $("#coverLoader").removeClass("show");
            },
            error: function(jxXHR, status, err) {
                $("#codeError").addClass("show");
                $("#coverLoader").removeClass("show");
                parent.resizeLoginScreen();
            },
            beforeSend: function (xhr) {
                xhr.setRequestHeader("Authorization", localStorage.getItem("token"));
                xhr.setRequestHeader("Password", password);
            },
            data: JSON.stringify(codePayload) + "\r\n\r\n"
        });
    }
}

function attemptDisable2FA() {
    $("#coverLoader").addClass("show");
    $("#codeError").removeClass("show");
    let password = $("#password").val();
    
    //All good, form the JSON and attempt to enable 2FA
    $.ajax({
        contentType: "application/json",
        timeout: 10000,
        type: "DELETE",
        url: "/api/users/me/2fa",
        success: function(data, status, jqXHR) {
            parent.toast("Two Factor Authentication", "Two Factor Authentication has been disabled.");
            resetView();
            $("#coverLoader").removeClass("show");
        },
        error: function(jxXHR, status, err) {
            $("#coverLoader").removeClass("show");
        },
        beforeSend: function (xhr) {
            xhr.setRequestHeader("Authorization", localStorage.getItem("token"));
            xhr.setRequestHeader("Password", password);
        }
    });
}