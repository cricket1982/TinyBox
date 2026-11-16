# Building TinyBox on GitHub and installing on the tablet (no cable)

After this one-time setup, each new version works like this:
1. Publish a Release on github.com.
2. GitHub builds the APK, which takes about 5 minutes.
3. Download and install it on the tablet.

## How signing works (read once)
Android only installs an update over an existing app if both are signed with the **same key**. If the keys differ, you have to uninstall first, and that deletes your saved projects.

So GitHub signs with the **same key Android Studio already uses on your PC**. That key lives in `C:\Users\dfresh\.android\debug.keystore`. As a result:
- the first GitHub build installs straight over the TinyBox you already have, and your projects stay
- Run from Android Studio and GitHub builds can be mixed freely

## 1. Create the repo
1. Sign in at github.com (or create a free account).
2. Click **+ → New repository**. Name it `TinyBox` and choose **Public**, so the tablet can download without logging in. Don't add a README. Click **Create repository**.

## 2. Upload the code with GitHub Desktop
1. Install **GitHub Desktop** (desktop.github.com) and sign in.
2. Choose **File → Add local repository** and pick your `TinyBox` folder. When it says the folder isn't a repository, click **create a repository**, then **Create repository**.
3. Click **Publish repository**. Untick "Keep this code private", and make sure the name matches the repo you created.

Later changes: GitHub Desktop shows what changed. Type a summary, click **Commit to main**, then **Push origin**.

## 3. Give GitHub your signing key (one secret)
1. On your PC, open **PowerShell** and run:
   ```powershell
   [Convert]::ToBase64String([IO.File]::ReadAllBytes("$env:USERPROFILE\.android\debug.keystore")) | Set-Clipboard
   ```
   This copies the key to the clipboard as text.
2. On github.com, go to your repo → **Settings → Secrets and variables → Actions → New repository secret**.
   - Name: `SIGNING_KEYSTORE_B64`
   - Secret: paste
   - Click **Add secret**.

You only need that one secret. The passwords are Android Studio's standard debug ones, which the build already knows.

## 4. Make a release
1. In your repo, go to **Releases** (right-hand side) → **Draft a new release**.
2. Click **Choose a tag** and type a new one, e.g. `v0.2`, then click **Create new tag**.
3. Give it a title, and notes if you like. Click **Publish release**.
4. The **Actions** tab shows the build running (about 5 minutes). When it goes green, `TinyBox-v0.2.apk` appears on the release.

Use a new, higher tag for each release (v0.3, v0.4 …).

## 5. Install on the tablet
1. On the tablet, open Chrome at `github.com/<your-username>/TinyBox/releases`. Bookmark it.
2. Tap the `.apk` under the newest release to download it, then tap the finished download.
3. The first time, Android asks to allow **Install unknown apps** for Chrome. Allow it, go back, and tap **Update** / **Install**.

## If something goes wrong
- **Build fails with "Secret SIGNING_KEYSTORE_B64 is missing":** step 3 hasn't been done, or the secret name has a typo.
- **The tablet says "App not installed" or "conflicts with an existing package":** the installed app was signed with a different key. This happens if it came from another PC, or if Android Studio's debug key was regenerated. Fix it once by backing up your projects first (SAVE in the app), then uninstalling TinyBox and installing the APK. Every update after that installs normally.
- **Run a build without making a release:** go to **Actions → Build APK → Run workflow**. The APK appears under that run's **Artifacts** as a zip. That's handy for testing, but Releases are easier on the tablet.
- **Sharing with other people:** send them the Releases link. They can install it the same way. (The debug key is fine for a hobby app. If you ever publish on the Play Store, we'd make a proper private key first.)
