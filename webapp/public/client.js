console.log('Client-side code running');
document.getElementById("newsourcepage").style.display = "none";

const start_button = document.getElementById('new_button');
start_button.addEventListener('click', function(e) {
    console.log('new button was clicked');
    var button = document.getElementById('new_button');
    button.disabled = true;

    document.getElementById("controlpage").style.display = "none";
    document.getElementById("statuspage").style.display = "none";
    document.getElementById("newsourcepage").style.display = "block";
    
    button.disabled = false;
});

const stop_button = document.getElementById('stop_button');
stop_button.addEventListener('click', function(e) {
    console.log('stop button was clicked');
    var button = document.getElementById('stop_button');
    button.disabled = true;

    fetch('/stop_clicked',{method: 'POST'})
	.then(function(response) {
	    if (response.ok) {
		console.log('stop clicked confirmed');
		return;
	    }
	    throw new Error('Stop request failed.');
	})
	.catch(function(error) {
	    console.log(error);
	});

});

const reset_button = document.getElementById('reset_button');
reset_button.addEventListener('click', function(e) {
    console.log('reset button was clicked');
    var button = document.getElementById('reset_button');
    button.disabled = true;

    fetch('/reset_clicked',{method: 'POST'})
	.then(function(response) {
	    if (response.ok) {
		console.log('reset clicked confirmed');
		return;
	    }
	    throw new Error('Reset request failed.');
	})
	.catch(function(error) {
	    console.log(error);
	});

});

const submit_button = document.getElementById('submit_button');
submit_button.addEventListener('click', function(e) {
    console.log('submit button was clicked');
    
    document.getElementById("controlpage").style.display = "block";
    document.getElementById("statuspage").style.display = "block";
    document.getElementById("newsourcepage").style.display = "none";
    alert("Saved!");
});

const abort_button = document.getElementById('abort_button');
abort_button.addEventListener('click', function(e) {
    console.log('abort button was clicked');
    
    document.getElementById("controlpage").style.display = "block";
    document.getElementById("statuspage").style.display = "block";
    document.getElementById("newsourcepage").style.display = "none";    
    alert("Aborted!");    
});
