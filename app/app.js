let loggedIn = false;
let loginData;
let currentProject = "";
let currentBug = -1;
let isAdmin = false;
let md = new showdown.Converter({
    headerLevelStart: 2,
    simplifiedAutoLink: true,
    excludeTrailingPunctuationFromURLs: true,
    strikethrough: true,
    simpleLineBreaks: true,
    tables: true
});
let scrollCommentsToBottom = false;
let obfuscatedLink;
let fileBugAttachments = [];
let ws;

let mouseX, mouseY;

//Preload items
{
    var img1 = new Image();
    img1.src = "/images/loader.svg";

    var img2 = new Image();
    img2.src = "/images/list-add.svg";

    var img3 = new Image();
    img3.src = "/images/arrow-left.svg";

    var img4 = new Image();
    img4.src = "/images/application-menu.svg";

    var img5 = new Image();
    img5.src = "/images/dialog-ok.svg";

    var img6 = new Image();
    img6.src = "/images/start-over.svg";

    var img7 = new Image();
    img7.src = "/images/mail-attachment.svg";

    var img8 = new Image();
    img8.src = "/images/fatal.svg";

    var img9 = new Image();
    img9.src = "/images/critical.svg";

    var img10 = new Image();
    img10.src = "/images/high.svg";

    var img11 = new Image();
    img11.src = "/images/low.svg";

    var img12 = new Image();
    img12.src = "/images/trivial.svg";

    var img13 = new Image();
    img13.src = "/images/request.svg";

}

console.log("%cWARNING", "color: yellow; font-size: xx-large");
console.log("This console can change the indended behaviour of the bug reporting tool. Unless you know what you're doing, don't enter any commands here. They could be sending user account information to an attacker.");

function load() {
    try {
        if (window.showdown == undefined) {
            throw new Error("Showdown not loaded");
        }
        if (window.filterXSS == undefined) {
            throw new Error("XSS Filter not loaded");
        }

        window.addEventListener("online", function() {
            dismissDialog("dlgOffline");
        });

        window.addEventListener("offline", function() {
            showDialog("dlgOffline");
        });

        $("body").mousemove(function(e) {
            mouseX = e.pageX;
            mouseY = e.pageY;
        });
        $("body").click(function() {
            $("#cxMenu").removeClass("show");
        });
        autosize($(".autogrow"));

        $("#commentText").keypress(function(e) {
            if (e.key == "Enter") {
                if (e.shiftKey) {
                    e.shiftKey = false;
                } else {
                    e.preventDefault();
                    sendComment();
                }
            }
        });

        $("#editCommentText").keydown(function(e) {
            if (e.key == "Enter") {
                if (e.ctrlKey) {
                    editCommentCommit();
                    e.preventDefault();
                }
            } else if (e.key == "Escape") {
                dismissDialog('dlgEdit');
            }
        });

        let statReporter = $("#loadingStatus");
        statReporter.html("Connecting to the server...");

        $.ajaxSetup({
            beforeSend: function (xhr) {
                let token = localStorage.getItem("token");
                if (token != null && token != "null") {
                    xhr.setRequestHeader("Authorization", token);
                }
            }
        });

        $("#newBug").on({
            "dragover": function(event) {
                event.preventDefault();
                event.stopPropagation();
                $("#newBugDropTarget").addClass("show");
            }
        });
        $("#commentsBox").on({
            "dragover": function(event) {
                event.preventDefault();
                event.stopPropagation();
                $("#commentsDropTarget").addClass("show");
            }
        });
        $(".dropTarget").on({
            "dragleave": function(event) {
                event.preventDefault();
                event.stopPropagation();
                $("#newBugDropTarget").removeClass("show");
                $("#commentsDropTarget").removeClass("show");
            },
            "drop": function(event) {
                $("#newBugDropTarget").removeClass("show");
                $("#commentsDropTarget").removeClass("show");

                //Upload files
                event.preventDefault();
                event.stopPropagation();
                uploadNewBugAttachment(event.originalEvent.dataTransfer.files);
            }
        });


        //Connect to WebSockets
        connectWs();
        ws.onopen = function() {
            statReporter.html("Retrieving tickets...");
            //Retrieve projects
            $.ajax("/api/projects", {
                success: function(data, status, jqXHR) {
                    for (key in data) {
                        let obj = data[key];
                        $('<div/>', {
                            html: '<img src="' + obj.icon + '" height="32" width="32" style="float: left"/>' + obj.name,
                            id: obj.name.replace(/ /g, "-")
                        }).click(function() {
                            $("#projects .selected").each(function() {
                                $(this).removeClass("selected");
                            });
                            $("#projects #" + obj.name.replace(/ /g, "-")).addClass("selected");

                            loadBugs(obj.name);
                            currentProject = obj.name;
                            currentBug = -1;
                            $("#newBugProjectName").text(obj.name);

                            $("#contentContainer").removeClass("projects");
                            $("#contentContainer").addClass("bugs");
                        }).appendTo("#projects");
                    }

                    statReporter.html("Logging in...");
                    reloadLoginState().then(function() {
                        $("#initialLanding").addClass("hide");
                        $("#headerButtons").addClass("show");
                        $("#initialLanding").on("animationend", function() {
                            $("#initialLanding").addClass("hidden");
                        });

                        if (getParameterByName("validated") == "true" && loggedIn) {
                            $("#genericOkWarningTitle").html("Validation");
                            $("#genericOkWarningText").html("Thanks, your email is now validated.");
                            showDialog("dlgGenericOkWarning")
                        }
                    }).catch(function() {
                        $("#initialLanding").addClass("hide");
                        $("#headerButtons").addClass("show");
                        $("#initialLanding").on("animationend", function() {
                            $("#initialLanding").addClass("hidden");
                        });
                    });
                },
                error: function(jxXHR, status, err) {
                    if (jxXHR.status == 429) {
                        ratelimit(jxXHR.getResponseHeader("x-ratelimit-reset"));
                        return;
                    }
                    showDialog("dlgLoadFail");
                },
                contentType: "application/json",
                timeout: 10000
            });
        }
        ws.onerror = function() {
            //Check rate limit
            $.ajax("/api/projects", {
                success: function(data, status, jqXHR) {
                    showDialog("dlgConnectionFail");
                },
                error: function(jxXHR, status, err) {
                    if (jxXHR.status == 429) {
                        ratelimit(jxXHR.getResponseHeader("x-ratelimit-reset"));
                        return;
                    }
                    showDialog("dlgConnectionFail");
                },
                contentType: "application/json",
                timeout: 10000
            });
        }
        ws.onclose = function() {
            showDialog("dlgDisconnected");
        }
        
    } catch (err) {
        document.getElementById("dlgLoadFail").classList.add("show");
        document.getElementById("blanker").classList.add("show");
        throw err;
    }
}

function connectWs() {
    ws = new WebSocket((window.location.protocol == "http:" ? "ws" : "wss") + "://" + window.location.host + "/api/socket");
    ws.onmessage = function(event) {
        //console.log("WS: " + event.data);

        if (event.data == "ERROR") {
            //Uh oh
            console.log("WebSocket Error: " + event.data.split("\n")[1]);
            toast("WebSocket Error", event.data.split("\n")[1]);
        } else if (event.data.startsWith("OK")) {
            //Log ok message
            console.log(event.data.split("\n")[1]);
        } else if (event.data.startsWith("COMMENT ")) {
            let dataString = event.data.substr(8);
            if (dataString.startsWith("NEW")) {
                dataString = dataString.substr(4);
                let commentObject = JSON.parse(dataString);
                if (commentObject.author != loginData.id) {
                    appendCommentToUi(commentObject);
                }
            }
        }
    }
    return ws;
}

function uploadNewBugAttachment(files) {
    let fileDescriptors = [];

    for (let i = 0; i < files.length; i++) {
        let file = files[i];

        let wrapper = $("<div/>", {
            class: "uploadObject"
        }).appendTo($("#newBugFileUploads"));
        $("<span/>", {
            text: file.name,
        }).appendTo(wrapper);
        let progressText = $("<span/>", {
            text: "Starting upload...",
            class: "uploadText"
        }).appendTo(wrapper);
        let progress = $("<div/>", {
            class: "progressBar indeterminate"
        }).appendTo(wrapper);
        let progressTrack = $("<div/>", {
            class: "progressBarTrack"
        }).appendTo(progress);

        fileDescriptors.push({
            wrapper: wrapper,
            progress: progress,
            progressTrack: progressTrack,
            progressText: progressText
        });
    }

    uploadAttachments(files, function(index, done, total) {
        let descriptor = fileDescriptors[index];
        if (total == -1) {
            descriptor.progress.addClass("indeterminate");
            descriptor.progressText.text("Uploading...");
        } else {
            descriptor.progress.removeClass("indeterminate");
            descriptor.progressTrack.css("width", done / total * 100 + "%");
            descriptor.progressText.text(calculateSize(done) + " / " + calculateSize(total));
        }
    }, function(index, fileId) {
        let descriptor = fileDescriptors[index];
        descriptor.progressText.text("Done");
        descriptor.progress.hide();
    }, function(index) {
        let descriptor = fileDescriptors[index];
        descriptor.progressText.text("Failed");
        descriptor.progress.addClass("error");
    });
}

function patchBug(data, projectName, id) {
    return new Promise(function(resolve, reject) {
        $.ajax("/api/bugs/" + projectName + "/" + id + "/", {
            success: function(data, status, jqXHR) {
                resolve(data);
            },
            error: function(jxXHR, status, err) {
                if (jxXHR.status == 429) {
                    ratelimit(jxXHR.getResponseHeader("x-ratelimit-reset"));
                    return;
                }
                reject();
            },
            contentType: "application/json",
            timeout: 10000,
            type: "PATCH",
            data: JSON.stringify(data) + "\r\n\r\n"
        });
    });
}

function loadBugs(projectName) {
    $("#bugs").find("*").not(".coverLoader, .loader").remove();
    $("#coverLoaderBugs").addClass("show");
    $("#bugInfo").removeClass("details");
    
    //Retrieve bugs
    $.ajax("/api/bugs/" + projectName + "/", {
        success: function(data, status, jqXHR) {
            $("#bugs").find("*").not(".coverLoader, .loader").remove();
            $('<div/>', {
                html: '<img src="/images/arrow-left.svg" />Go Back',
                class: "parentButton"
            }).click(function() {
                $("#contentContainer").removeClass("bugs");
                $("#contentContainer").removeClass("bugInfo");
                $("#contentContainer").addClass("projects");
            }).appendTo("#bugs");

            for (key in data) {
                let obj = data[key];

                let listItem;
                let cxMenu = [
                        {
                            text: "For ticket #" + obj.id,
                            class: "menuHeader"
                        },
                        {
                            text: "Close",
                            class: "menuItem",
                            click: function() {
                                patchBug({
                                    isOpen: false
                                }, projectName, obj.id).then(function() {
                                    obj.isOpen = false;
                                    listItem.addClass("closed");
                                    toast("Close Ticket", "Ticket #" + obj.id + " closed.", [
                                        {
                                            text: "Undo",
                                            onclick: function() {
                                                dismissToast();
                                                patchBug({
                                                    isOpen: true
                                                }, projectName, obj.id).then(function() {
                                                    obj.isOpen = true;
                                                    listItem.removeClass("closed");
                                                });
                                            }
                                        }
                                    ]);
                                }).catch(function(err) {
                                    toast("Close Ticket", "Couldn't close ticket.");
                                });
                            },
                            showIf: function() {
                                if (!isAdmin && obj.author != loginData.id) return false;
                                if (!obj.isOpen) return false;
                                return true;
                            }
                        },
                        {
                            text: "Re-open",
                            class: "menuItem",
                            click: function() {
                                patchBug({
                                    isOpen: true
                                }, projectName, obj.id).then(function() {
                                    listItem.removeClass("closed");
                                    obj.isOpen = true;
                                    toast("Re-open Ticket", "Ticket #" + obj.id + " re-opened.", [
                                        {
                                            text: "Undo",
                                            onclick: function() {
                                                dismissToast();
                                                patchBug({
                                                    isOpen: false
                                                }, projectName, obj.id).then(function() {
                                                    obj.isOpen = false;
                                                    listItem.addClass("closed");
                                                });
                                            }
                                        }
                                    ]);
                                }).catch(function() {
                                    toast("Re-open Ticket", "Couldn't re-open ticket.");
                                });
                            },
                            showIf: function() {
                                if (!isAdmin && obj.author != loginData.id) return false;
                                if (obj.isOpen) return false;
                                return true;
                            }
                        },
                        {
                            text: "Delete",
                            class: "menuItem",
                            click: function() {
                                toast("Stub", "Delete");
                            },
                            showIf: function() {
                                if (!isAdmin) return false;
                                return true;
                            }
                        }
                    ];

                listItem = $('<div/>', {
                    html: obj.title + '&nbsp;',
                    id: obj.id,
                    class: obj.isOpen ? "" : "closed"
                }).click(function() {
                    $("#bugs .selected").each(function() {
                        $(this).removeClass("selected");
                    });
                    $("#bugs #" + obj.id).addClass("selected");

                    loadBug(projectName, obj.id);
                    $("#contentContainer").removeClass("bugs");
                    $("#contentContainer").addClass("bugInfo");
                }).contextmenu(function() {
                    showContextMenu(cxMenu);
                }).appendTo("#bugs");

                $("<span/>", {
                    class: "listId",
                    html: "#" + obj.id
                }).appendTo(listItem);

                let auxSpan = $("<span/>", {
                    class: "aux options"
                }).appendTo(listItem);

                if (obj.importance != 3) {
                    let importanceIcon = $("<img/>").appendTo(auxSpan);
                    switch (obj.importance) {
                        case 0: //Fatal
                            importanceIcon.attr("src", "/images/fatal.svg");
                            break;
                        case 1: //Critical
                            importanceIcon.attr("src", "/images/critical.svg");
                            break;
                        case 2: //High
                            importanceIcon.attr("src", "/images/high.svg");
                            break;
                        case 4: //Low
                            importanceIcon.attr("src", "/images/low.svg");
                            break;
                        case 5: //Trivial
                            importanceIcon.attr("src", "/images/trivial.svg");
                            break;
                        case 6: //Feature Request
                            importanceIcon.attr("src", "/images/request.svg");
                            break;
                    }
                }
                if (obj.private) {
                    $("<img/>", {
                        src: "/images/visibility.svg"
                    }).appendTo(auxSpan);
                }
                $("<img/>", {
                    src: "/images/application-menu.svg"
                }).click(function(e) {
                    e.stopPropagation();
                    showContextMenu(cxMenu);
                }).appendTo(auxSpan);
            }

            $('<div/>', {
                html: '<img src="/images/list-add.svg" />File New Ticket'
            }).click(function() {
                fileBug();
            }).appendTo("#bugs");

            $("#initialLanding").addClass("hide");
            $("#initialLanding").on("animationend", function() {
                $("#initialLanding").addClass("hidden");
            });
            $("#coverLoaderBugs").removeClass("show");
        },
        error: function(jxXHR, status, err) {
            if (jxXHR.status == 429) {
                ratelimit(jxXHR.getResponseHeader("x-ratelimit-reset"));
                return;
            }

            toast("Error", "Couldn't load tickets for project " + projectName + ".", [
                {
                    text: "More Info",
                    onclick: function() {
                        $("#genericOkWarningTitle").html("Error Details");
                        $("#genericOkWarningText").html(err);
                        showDialog("dlgGenericOkWarning")
                    }
                }
            ]);
        },
        beforeSend: function (xhr) {
            if (localStorage.hasOwnProperty("token")) {
                xhr.setRequestHeader("Authorization", localStorage.getItem("token"));
            }
        },
        contentType: "application/json",
        timeout: 10000
    });
}

function loadBug(projectName, id) {
    currentBug = -1;
    $("#bugLoader").css("display", "flex");
    $("#bugBodyWrapper").hide();
    $("#bugTop").hide();
    $("#bugComments").empty();
    $("#bugInfo").addClass("details");
    $("#commentsWrapper").hide();

    ws.send("UPDATES PROJECT " + projectName);
    ws.send("UPDATES BUG " + id);
    $.ajax("/api/bugs/" + projectName + "/" + id + "/", {
        success: function(data, status, jqXHR) {
            $("#bugBodyWrapper").show();
            $("#bugTop").show();
            $("#bugComments").empty();
            $("#bugTitle").text(data.title);
            $("#bugBody").html(filterCommentContents(data.body));
            $("#bugTimestamp").text(data.timestamp);
            $("#bugReporter").text("...");
            if (data.isOpen) {
                $("#bugOpen").removeClass();
                $("#bugOpen").addClass("bugOpen");
            } else {
                $("#bugOpen").removeClass();
                $("#bugOpen").addClass("bugClosed");
            }

            let importance;
            switch (data.importance) {
                case 0:
                    importance = "Fatal";
                    break;
                case 1:
                    importance = "Critical";
                    break;
                case 2:
                    importance = "High";
                    break;
                case 3:
                    importance = "Normal";
                    break;
                case 4:
                    importance = "Low";
                    break;
                case 5:
                    importance = "Trivial";
                    break;
                case 6:
                    importance = "Feature Request";
                    break;
            }
            $("#bugImportance").text(importance);

            getUser(data.author).then(function(user) {
                $("#bugReporter").text(user.username);
                $("#bugProfilePicture").attr("src", user.picture + "&s=16");
            }).catch(function(err) {
                $("#bugReporter").text("Deleted Account");
            });
            currentBug = data.id;

            if (isAdmin) {
                if (data.isOpen) {
                    $("#btnCloseBug").css("display", "inline-flex");
                    $("#btnReopenBug").css("display", "none");
                } else {
                    $("#btnCloseBug").css("display", "none");
                    $("#btnReopenBug").css("display", "inline-flex");
                }
            } else {
                $("#btnCloseBug").css("display", "none");
                $("#btnReopenBug").css("display", "none");
            }

            //Get comments
            $.ajax({
                contentType: "application/json",
                timeout: 10000,
                type: "GET",
                url: "/api/bugs/" + currentProject + "/" + currentBug + "/comments",
                success: function(data, status, jqXHR) {
                    $("#bugComments").empty();
                    $("#bugLoader").css("display", "none");
                    $("#commentsWrapper").show();

                    for (let key in data) {
                        appendCommentToUi(data[key]);
                    }

                    if (scrollCommentsToBottom) {
                        scrollCommentsToBottom = false;
                        $("#bugInfo").scrollTop($("#bugInfo").prop("scrollHeight"));
                    }
                },
                error: function(jxXHR, status, err) {
                    if (jxXHR.status == 429) {
                        ratelimit(jxXHR.getResponseHeader("x-ratelimit-reset"));
                        return;
                    }

                    $("<div/>", {
                        contents: "Failed to load comments"
                    }).appendTo($("#bugComments"));
                }
            });
        },
        error: function(jxXHR, status, err) {
            if (jxXHR.status == 429) {
                ratelimit(jxXHR.getResponseHeader("x-ratelimit-reset"));
                return;
            }

            toast("Error", "Couldn't load ticket " + id + " for project " + projectName + ".", [
                {
                    text: "More Info",
                    onclick: function() {
                        $("#genericOkWarningTitle").html("Error Details");
                        $("#genericOkWarningText").html(err);
                        showDialog("dlgGenericOkWarning")
                    }
                }
            ]);
        },
        contentType: "application/json",
        timeout: 10000
    });
}

function fileBug() {
    if (!loggedIn) {
        showDialog("dlgLoginRequired");
    } else if (currentProject == "") {
        $("#genericOkWarningTitle").html("Select Project");
        $("#genericOkWarningText").html("To start filing a ticket, go ahead and select the project it should be filed under.");
        showDialog("dlgGenericOkWarning")
    } else {
        $("#newBug").addClass("show");
        $("#newBugTitle").focus();
    }
}

function cancelFileBug() {
    $("#newBug").removeClass("show");
    $("#newBugTitle").val("");
    $("#newBugContent").val("");
    $("#newBugFileUploads").empty();
    $("#newBugImportance").val("3");
    $("#publicBug").prop("checked", true);
    fileBugAttachments = [];
}

var toastTimeout = null;
var finishToastFunction = null;
function toast(title, message, buttons, finishToast = function(){}) {
    $("#toast-header").html(title);
    $("#toast-contents").html(message);

    $("#toast-buttons").empty();
    for (key in buttons) {
        let button = buttons[key];

        $('<a/>', {
            html: button.text,
            href: "#",
            class: "button"
        }).click(button.onclick).appendTo("#toast-buttons");
    }

    $("#toast").addClass("show");

    if (toastTimeout != null) {
        finishToastFunction();
        clearTimeout(toastTimeout);
    }

    finishToastFunction = finishToast;
    toastTimeout = setTimeout(function() {
        finishToast();
        dismissToast();
    }, 5000);
}

function dismissToast() {
    clearTimeout(toastTimeout);
    $("#toast").removeClass("show");
    toastTimeout = null;
}

$(document).contextmenu(function() {
    return false;
});


function login() {
    dismissDialog('dlgLoginRequired');
    showDialog('dlgLogin');
}

function reloadLoginState() {
    return new Promise(function(resolve, reject) {
    let token = localStorage.getItem("token");
    if (token == null || token == "null") { //Logged Out
        loggedIn = false;
        $("#userSettings").text("Log In");
        isAdmin = false;
        ws.send("DEAUTHENTICATE");
        $("body").removeClass("adminMode");
        resolve();
    } else {
        //Retrieve user settings
        $.ajax("/api/users/me", {
            success: function(data, status, jqXHR) {
                $("#userSettings").text(data.username);
                $("#userManagementUsername").text(data.username);
                loggedIn = true;
                loginData = data;
                isAdmin = data.isAdmin;

                ws.send("AUTHENTICATE " + token);
                if (isAdmin) {
                    $("body").addClass("adminMode");
                } else {
                    $("body").removeClass("adminMode");
                }

                resolve();
            },
            error: function(jxXHR, status, err) {
                if (jxXHR.status == 429) {
                    ratelimit(jxXHR.getResponseHeader("x-ratelimit-reset"));
                    return;
                }

                ws.send("DEAUTHENTICATE");
                localStorage.setItem("token", null);

                $("#genericOkWarningTitle").html("Login Error");
                $("#genericOkWarningText").html("We couldn't log you in. Try logging in again.");
                showDialog("dlgGenericOkWarning")

                $("#userSettings").text("Log In");
                loggedIn = false;
                reject();
            },
            contentType: "application/json",
            timeout: 10000
        });
    }
    });
}

function userSettings() {
    if (loggedIn) {
        showDialog("dlgUserManagement");
    } else {
        login();
    }
}

function logout() {
    dismissDialog('dlgLogout');
    localStorage.setItem("token", null);
    reloadLoginState();

    toast("Log Out", "You have logged out successfully.");
}

$(window).on('beforeunload', function() {
    if ($("#newBug").hasClass("show")) {
        return "You're currently drafting a ticket. Do you want to discard it?";
    }
});

let users = {};
function getUser(userId) {
    return new Promise(function(resolve, reject) {
        if (users.hasOwnProperty(userId)) {
            if (users[userId] == "deleted") {
                reject();
            } else {
                resolve(users[userId]);
            }
        } else {
            //Retrieve user settings
            $.ajax("/api/users/" + userId, {
                success: function(data, status, jqXHR) {
                    users[userId] = {
                        username: data.username,
                        picture: data.picture,
                        isAdmin: data.isAdmin
                    }

                    resolve(users[userId]);
                },
                error: function(jxXHR, status, err) {
                    if (jxXHR.status == 429) {
                        ratelimit(jxXHR.getResponseHeader("x-ratelimit-reset"));
                        return;
                    }

                    if (jxXHR.status == 404) {
                        users[userId] = "deleted";
                    }
                    reject();
                },
                contentType: "application/json",
                timeout: 10000
            });
        }
    });
}

function deleteAccount() {
    $("#deleteAccountError").removeClass("show");
    //Delete User
    $.ajax("/api/users/me", {
        success: function(data, status, jqXHR) {
            dismissDialog('dlgDeleteAccount');
            $("#deleteAccountPassword").val("");
            localStorage.setItem("token", null);
            reloadLoginState();

            toast("Account Deleted", "Your account has been deleted successfully.");
        },
        error: function(jxXHR, status, err) {
            if (jxXHR.status == 429) {
                ratelimit(jxXHR.getResponseHeader("x-ratelimit-reset"));
                return;
            }

            $("#deleteAccountError").addClass("show");
        },
        beforeSend: function (xhr) {
            xhr.setRequestHeader("Authorization", localStorage.getItem("token"));
            xhr.setRequestHeader("Password", $("#deleteAccountPassword").val());
        },
        contentType: "application/json",
        timeout: 10000,
        type: "DELETE"
    });
}

function doFileBug() {
    let title = $("#newBugTitle").val();
    let body = $("#newBugContent").val();

    if (title == "" || body == "") {
        toast("File Ticket", "You'll need to fill in the title and the body of the ticket.");
        return;
    }

    //All good, form the bug and send it off
    let bugPayload = {
        title: title,
        body: body,
        importance: parseInt($("#newBugImportance").val()),
        private: $("#privateBug").prop("checked")
    }

    $.ajax({
        contentType: "application/json",
        timeout: 10000,
        type: "POST",
        url: "/api/bugs/" + currentProject + "/create",
        success: function(data, status, jqXHR) {
            cancelFileBug();
            toast("File Ticket", "Ticket filed successfully. We'll take a look at it soon.");

            loadBugs(currentProject);
        },
        error: function(jxXHR, status, err) {
            if (jxXHR.status == 429) {
                ratelimit(jxXHR.getResponseHeader("x-ratelimit-reset"));
                return;
            }

            toast("File Ticket", "Couldn't file ticket.");
        },
        data: JSON.stringify(bugPayload) + "\r\n\r\n"
    });
}

function resizeLoginScreen() {
    //noop
    //$("#loginiFrame").height(height);
}

function showContextMenu(menuData, x = mouseX, y = mouseY) {
    $("#cxMenu").empty();

    for (let key in menuData) {
        let def = menuData[key];

        if (def.hasOwnProperty("showIf")) {
            if (!def.showIf()) {
                continue;
            }
        }

        let item = $("<div/>", {
            class: def.class,
            text: def.text
        }).appendTo("#cxMenu");

        if (def.hasOwnProperty("click")) {
            item.click(def.click);
        }
    }

    $("#cxMenu").addClass("show");
    $("#cxMenu").css({
        left: x + "px",
        top: y + "px"
    });

    if ($("#cxMenu").position().left + $("#cxMenu").width() > $(window).width()) {
        $("#cxMenu").css("left", ($(window).width() - $("#cxMenu").width() - 10) + "px")
    }

}

function sendComment() {
    if (!loggedIn) {
        showDialog("dlgLoginRequired");
    } else {

        //Write Comment
        let commentPayload = {
            body: $("#commentText").val()
        };

        let pendingCommentDescriptor = {
            system: false,
            body: $("#commentText").val(),
            sending: true
        }
        let commentElement = appendCommentToUi(pendingCommentDescriptor);
        $("#bugInfo").animate({
            scrollTop: $('#bugInfo').prop("scrollHeight")
        }, 2000, "easeOutCubic");
        $("#commentText").val("");
        autosize.update($("#commentText"));

        $.ajax({
            contentType: "application/json",
            timeout: 10000,
            type: "POST",
            url: "/api/bugs/" + currentProject + "/" + currentBug + "/comments",
            success: function(data, status, jqXHR) {
                editComment(commentElement, data);
            },
            error: function(jxXHR, status, err) {
                if (jxXHR.status == 429) {
                    ratelimit(jxXHR.getResponseHeader("x-ratelimit-reset"));
                    return;
                }

                //toast("Comment", "Couldn't write comment on this issue.");
                pendingCommentDescriptor.sending = false;
                pendingCommentDescriptor.error = true;
                pendingCommentDescriptor.errorText = "Catastrophic Failure";
                editComment(commentElement, pendingCommentDescriptor);
            },
            data: JSON.stringify(commentPayload) + "\r\n\r\n"
        });
    }
}

function filterCommentContents(input) {
    let html = $(md.makeHtml(filterXSS(input, {
        escapeHtml: function(html) {
            let newHtml = "";
            let currentLeftBrackets = 0;
            for (let i = 0; i < html.length; i++) {
                let character = html[i];
                if (character == "<") {
                    currentLeftBrackets++;
                    newHtml += "&lt;";
                } else if (character == ">" && currentLeftBrackets != 0) {
                    currentLeftBrackets--;
                    newHtml += "&gt;";
                } else {
                    newHtml += character;
                }
            }
            return newHtml;

            return html.replace(/</g, '&lt;').replace(/>/g, '&gt;');
            debugger;
        }
    })));
    html.children("a").each(function() {
        let link = this.href;
        this.href = "#";
        $(this).click(function() {
            $("#linkDialogLink").text(link);
            obfuscatedLink = link;
            showDialog("dlgLink");
        });
    });

    let blockquoteFormat = function() {
        let element = $(this);
        getUser(this.textContent).then(function(user) {
            element.replaceWith($("<span/>", {
                text: user.username + " said",
                class: "author"
            }));
        }).catch(function() {
            element.replaceWith($("<span/>", {
                text: "Deleted User said",
                class: "author"
            }));
        });
    };
    html.children("blockquote h2:first-child").each(blockquoteFormat);
    html.find("blockquote h2:first-child").each(blockquoteFormat);
    return html;
}

function visitObfuscatedLink() {
    dismissDialog('dlgLink');
    window.open(obfuscatedLink);
}

function patchCurrentBug(data) {
    patchBug(data, currentProject, currentBug);
    scrollCommentsToBottom = true;
    loadBug(currentProject, currentBug);
}

function editComment(wrapper, comment) {
    wrapper.empty();
    wrapper.removeClass("disabled");
    wrapper.removeClass("error");
    if (comment.system) {
        wrapper.addClass("bugSystem");
        wrapper.removeClass("bugBody");
        let image;

        switch (comment.id) {
            case "-1":
                image = "/images/dialog-ok.svg";
                break;
            case "-2":
                image = "/images/start-over.svg";
                break;
        }

        $("<img/>", {
            src: image
        }).appendTo(wrapper);
        $("<p/>", {
            text: comment.body
        }).appendTo(wrapper);
        return wrapper;
    } else {
        wrapper.removeClass("bugSystem");
        wrapper.addClass("bugBody");

        let bodyDiv = $("<div/>", {
            class: "bugContents",
            html: filterCommentContents(comment.body)
        }).appendTo(wrapper);

        let metadata = $("<div/>", {
            class: "bugBodyMetadata"
        }).appendTo(wrapper);
        let shield = $("<img/>", {
            src: "/images/administrator.svg",
            class: "shield"
        }).hide().appendTo(metadata);
        let profile = $("<img/>", {
            class: "profile small"
        }).appendTo(metadata);
        let author = $("<span/>", {
            text: "..."
        }).appendTo(metadata);

        if (comment.sending) {
            wrapper.addClass("disabled");
            author.text("Sending...");
        } else if (comment.error) {
            wrapper.addClass("error");
            author.text("Couldn't send comment");

            metadata.append(" &bullet; ");

            $("<span/>", {
                text: comment.errorText
            }).appendTo(metadata);
        } else {
            metadata.append(" &bullet; ");

            $("<span/>", {
                text: comment.timestamp
            }).appendTo(metadata);

            getUser(comment.author).then(function(user) {
                author.text(user.username);

                if (user.isAdmin) {
                    shield.show();
                }
                
                profile.attr("src", user.picture + "&s=32");
            }).catch(function(err) {
                author.text("Deleted Account");
            });
        }

        $("<img/>", {
            src: "/images/application-menu.svg"
        }).click(function(e) {
            e.stopPropagation();
            showContextMenu([
                        {
                            text: "For comment",
                            class: "menuHeader"
                        },
                        {
                            text: "Reply",
                            class: "menuItem",
                            click: function() {
                                let value = "> #" + comment.author + "\n";
                                let lines = comment.body.split("\n");
                                for (let key in lines) {
                                    value += "> " + lines[key] + "\n";
                                }
                                value += "\n";
                                $("#commentText").val(value)
                                autosize.update($("#commentText"));
                                $("#commentText").focus();
                            },
                            showIf: function() {
                                return true;
                            }
                        },
                        {
                            text: "Edit",
                            class: "menuItem",
                            click: function() {
                                $("#editCommentText").val(comment.body);
                                editingComment = {
                                    id: comment.id,
                                    bodyDiv: bodyDiv,
                                    wrapper: wrapper
                                }
                                showDialog("dlgEdit");
                                $("#editCommentText").focus();
                            },
                            showIf: function() {
                                if (comment.author == loginData.id) {
                                    return true;
                                }
                                return false;
                            }
                        },
                        {
                            text: "Delete",
                            class: "menuItem",
                            click: function() {
                                wrapper.addClass("hide");
                                toast("Delete Comment", "Comment Deleted",  [
                                        {
                                            text: "Undo",
                                            onclick: function() {
                                                dismissToast();
                                                wrapper.removeClass("hide");
                                            }
                                        }
                                    ], function() {
                                        //Actually delete the comment

                                        $.ajax("/api/bugs/" + currentProject + "/" + currentBug + "/comments/" + comment.id, {
                                            error: function(jxXHR, status, err) {
                                                if (jxXHR.status == 429) {
                                                    ratelimit(jxXHR.getResponseHeader("x-ratelimit-reset"));
                                                    return;
                                                }
                                            },
                                            beforeSend: function (xhr) {
                                                xhr.setRequestHeader("Authorization", localStorage.getItem("token"));
                                            },
                                            contentType: "application/json",
                                            timeout: 10000,
                                            type: "DELETE"
                                        });
                                    });
                            },
                            showIf: function() {
                                if (comment.author == loginData.id || isAdmin) {
                                    return true;
                                }
                                return false;
                            }
                        }
                    ]);
        }).appendTo(metadata);

        return wrapper;
    }
}

function appendCommentToUi(comment) {
    let wrapper = $("<div/>").appendTo($("#bugComments"));
    editComment(wrapper, comment);
    return wrapper;
}

function ratelimit(resetHeader) {
    let date = new Date(0);
    date.setUTCSeconds(resetHeader);
    $("#ratelimitReset").text(date.toString());
    showDialog("dlgRatelimit");
}

function uploadAttachments(files, progress, done, error) {
    for (let i = 0; i < files.length; i++) {
        let file = files[i];
        $.ajax({
            xhr: function() {
                var xhr = new window.XMLHttpRequest();

                xhr.upload.addEventListener("progress", function(evt) {
                    if (evt.lengthComputable) {
                        progress(i, evt.loaded, evt.total);
                    } else {
                        progress(0, -1);
                    }
                }, false);

                return xhr;
            },
            url: "/api/files/upload",
            type: "POST",
            data: file,
            cache: false,
            contentType: false,
            processData: false,
            success: function(data, status, jqXHR) {
                done(i);
            },
            error: function(jxXHR, status, err) {
                error(i);
            },
            beforeSend: function (xhr) {
                let token = localStorage.getItem("token");
                xhr.setRequestHeader("Authorization", token);
                xhr.setRequestHeader("FileName", file.name);
            }
        });
    }
}

var editingComment;
function editCommentCommit() {
    let editObject = editingComment;
    dismissDialog("dlgEdit")
    let editedComment = $("#editCommentText").val();
    if (editedComment != "") {
            let editPayload = {
                body: editedComment
            }
            editObject.bodyDiv.html(filterCommentContents(editedComment));
            editObject.wrapper.addClass("disabled");
            $.ajax("/api/bugs/" + currentProject + "/" + currentBug + "/comments/" + editObject.id, {
                error: function(jxXHR, status, err) {
                    if (jxXHR.status == 429) {
                        ratelimit(jxXHR.getResponseHeader("x-ratelimit-reset"));
                        return;
                    }

                    editObject.wrapper.removeClass("disabled");
                    editObject.wrapper.addClass("error");
                },
                beforeSend: function (xhr) {
                    xhr.setRequestHeader("Authorization", localStorage.getItem("token"));
                },
                success: function(data, status, jxXHR) {
                    editObject.wrapper.removeClass("disabled");
                },
                contentType: "application/json",
                timeout: 10000,
                type: "PATCH",
                data: JSON.stringify(editPayload) + "\r\n\r\n"
            });

    }
}

function calculateSize(size) {
    if (size > 1073741824) {
        return Math.round(size / 1024 / 1024 / 1024 * 100) / 100 + " GiB";
    } else if (size > 1048576) {
        return Math.round(size / 1024 / 1024 * 100) / 100 + " MiB";
    } else if (size > 1024) {
        return Math.round(size / 1024 * 100) / 100 + " KiB";
    } else {
        return Math.round(size * 100) / 100 + " B";
    }
}

function getParameterByName(name) {
    let url = window.location.href;
    name = name.replace(/[\[\]]/g, "\\$&");
    var regex = new RegExp("[?&]" + name + "(=([^&#]*)|&|#|$)"),
        results = regex.exec(url);
    if (!results) return null;
    if (!results[2]) return '';
    return decodeURIComponent(results[2].replace(/\+/g, " "));
}