function showDialog(id) {
    $("#" + id).addClass("show");
    $("#blanker").addClass("show");
}

function dismissDialog(id) {
    $("#" + id).removeClass("show");
    $("#blanker").removeClass("show");
}
