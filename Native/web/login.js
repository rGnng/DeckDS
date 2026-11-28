const dialog = document.getElementById("sign-in-dialog");
const form = document.getElementById("sign-in-form");
const password = document.getElementById("sign-in-password");
const submit = document.getElementById("sign-in-submit");
const error = document.getElementById("sign-in-error");

dialog.addEventListener("cancel", (event) => event.preventDefault());
dialog.showModal();

form.addEventListener("submit", async (event) => {
  event.preventDefault();
  if (submit.disabled) return;
  error.textContent = "";
  password.disabled = submit.disabled = true;
  try {
    const response = await fetch("/api/access/login", {
      method: "POST",
      headers: { "Content-Type": "text/plain" },
      body: password.value,
    });
    const result = await response.json();
    if (!response.ok) throw new Error(result.error || "Could not sign in.");
    const next = new URLSearchParams(location.search).get("next");
    location.replace(next === "debug" ? "/debug" : "/");
  } catch (failure) {
    error.textContent = failure.message;
    password.disabled = submit.disabled = false;
    password.focus();
    password.select();
  }
});
