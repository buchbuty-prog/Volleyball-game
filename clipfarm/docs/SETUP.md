# ClipFarm setup guide: accounts, keys and platform approvals

Start the slow approvals early. They take weeks, and without them uploads stay
**private** (YouTube), restricted to your own accounts (Instagram), or **drafts**
(TikTok). ClipFarm works without them; it just can't make anything public.

> Platform rules change often. Where this guide gives a number or a rule, it was
> checked in Nov 2026, and sources disagreed on some of it (marked **verify**).
> ClipFarm reads limits from the APIs at runtime instead of hardcoding them.

## Summary

| Platform | What you create | Works before approval | Approval | What approval unlocks |
|---|---|---|---|---|
| Anthropic | API key ✅ (you have this) | Everything | none | n/a |
| Google / YouTube | Cloud project + OAuth client | Read your own and test users' channels; uploads may be **locked private** | ① OAuth app verification ② YouTube API Services audit + quota extension | ① Creators outside your test-user list can connect, and tokens stop expiring every 7 days ② Public uploads and more daily quota |
| Google Drive (intake) | Service account | Everything (creators share a folder with it) | none | n/a |
| Dropbox (intake) | Scoped Dropbox app | Up to 500 linked users in development | Production status | More than 500 users |
| Meta / Instagram | Business app + IG professional account linked to a Page | Publish to accounts where you have a role on the app (your own) | ③ Business verification + App Review (Advanced Access) | Publishing to **creators'** accounts |
| TikTok | Developer app with Content Posting API | Sandbox only | ④ App review (scopes) ⑤ Content Posting API audit | ④ Real users can connect, with inbox drafts ⑤ Direct, public posting |

**Before any review you need:** a public homepage, a privacy policy, terms of
service, a domain you can verify, and a public HTTPS URL for OAuth redirects. A
tunnel (Cloudflare Tunnel or ngrok) pointing to your local ClipFarm works for testing.

---

## 1. Google Cloud + YouTube (needed for steps 2, 6, 7)

1. Go to <https://console.cloud.google.com>, create a project named `clipfarm`.
2. **APIs & Services → Library**, enable:
   - YouTube Data API v3 (ranking videos, uploads)
   - YouTube Analytics API (views and watch time per clip, step 7)
   - Google Drive API (intake from shared folders)
3. **OAuth consent screen** → User type *External*. Add your app name, support email,
   homepage, privacy policy and authorised domain. Scopes ClipFarm will ask for:
   - `https://www.googleapis.com/auth/youtube.readonly`: creators connect read-only (step 2)
   - `https://www.googleapis.com/auth/youtube.upload`: your own channel, or creators' if you post to their accounts (step 6)
   - `https://www.googleapis.com/auth/yt-analytics.readonly`: metrics (step 7)
4. **Credentials → Create OAuth client ID → Web application.**
   Authorised redirect URI: `http://localhost:8000/oauth/google/callback` (plus your
   public HTTPS URL later). Put the values in `.env` as `GOOGLE_OAUTH_CLIENT_ID` /
   `GOOGLE_OAUTH_CLIENT_SECRET`.
5. While the consent screen is in **Testing**: add yourself and each creator as
   *test users* (max 100). Refresh tokens for test-mode apps expire after 7 days, so
   creators will have to reconnect weekly until the app is verified.

### Approval ①: OAuth app verification
*Consent screen → Publish app → Prepare for verification.* You'll need a verified
domain, privacy policy, a demo video of the OAuth flow, and a written explanation of
each scope. YouTube scopes count as *sensitive*. Unlocks: any creator can connect,
and tokens no longer expire every week.

### Approval ②: YouTube API Services audit and quota extension
Apply via the *YouTube API Services: Audit and Quota Extension Form*.
- **Private lock (verify):** videos uploaded through API projects created after July 28, 2020
  that haven't passed this audit have been **locked to private**. Some 2026 reports say
  this was lifted. Test with one upload; ClipFarm will show the resulting privacy status.
- **Quota:** default is 10,000 units/day. The cost of an upload (`videos.insert`) has been
  1,600 units, i.e. about 6 uploads/day (**verify**: newer reports say upload cost changed).
  ClipFarm tracks units used and queues uploads when the day's quota runs out.
- The audit checks compliance with the YouTube API Developer Policies. For kids'
  content, expect questions about how you set made-for-kids. ClipFarm always sends
  `status.selfDeclaredMadeForKids = true`, which also turns off comments, and you
  can't change that in the UI.

### Google Drive intake (no approval needed)
Create a **service account** (*IAM → Service accounts*), download its JSON key, and
give its path to ClipFarm (the variable is added in step 2). Each creator shares their
source-video folder with the service account's email as *Viewer*. This avoids the
`drive.readonly` user scope, which is *restricted* and would need a paid security
assessment.

## 2. Dropbox (optional, step 2)
<https://www.dropbox.com/developers/apps> → *Create app → Scoped access → App folder*
or *Full Dropbox*, with scopes `files.metadata.read`, `files.content.read`,
`sharing.read`. Put the key and secret in `DROPBOX_APP_KEY` / `DROPBOX_APP_SECRET`.
Development status allows up to 500 linked users; apply for production after that.

## 3. Meta / Instagram Reels (step 6, 7)

1. The Instagram account you post to must be a **professional account** (Business or
   Creator) **linked to a Facebook Page**. Do this for your own account, or ask each
   creator to if you post to theirs.
2. <https://developers.facebook.com> → *Create app* → type **Business**. Add the
   products **Instagram** (API setup with Facebook Login) and **Facebook Login for Business**.
3. Permissions ClipFarm will request (**verify** names in the dashboard; Meta has been
   renaming them): `instagram_basic`, `instagram_content_publish`,
   `instagram_manage_insights`, `pages_show_list`, `pages_read_engagement`.
4. Put the values in `META_APP_ID` / `META_APP_SECRET`.
5. Reels upload: Meta fetches the video from a URL or takes a resumable upload.
   Local MinIO isn't reachable from Meta, so ClipFarm will use the resumable upload
   (or a short-lived public link if you move storage to S3 or R2).
6. **Publishing limit:** before each post, ClipFarm calls
   `GET /{ig-user-id}/content_publishing_limit` and waits if the quota is used up. The
   number is not hardcoded (reported values vary between 50 and 100 posts per 24 h).

### Approval ③: Business verification and App Review
- *Standard Access* (no review) works only for Instagram accounts of people who have a
  role on your app. Add yourself as admin and you can post to **your own** accounts.
- To post to **creators'** accounts you need **Advanced Access**: complete **Business
  Verification** (company documents), then submit **App Review** with a screencast for
  each permission. Typically takes 2–4 weeks.
- Instagram requires users to be 13+. An account that features a child under 13
  must be run by a parent or manager, and should say so in its bio.

## 4. TikTok (step 6, 7)

1. <https://developers.tiktok.com> → create an app. Add products **Login Kit** and
   **Content Posting API**.
2. Scopes: `user.info.basic`, `video.upload` (send to inbox as drafts),
   `video.publish` (direct post, only after the audit), `video.list` (metrics).
3. Put the values in `TIKTOK_CLIENT_KEY` / `TIKTOK_CLIENT_SECRET`. TikTok requires a
   registered **HTTPS** redirect URI, so use your tunnel URL for local testing.
4. Use **Sandbox** mode to test with a few named TikTok accounts before review.

### Approval ④: App review
Submit the app (description, demo video, privacy policy and terms) so the scopes
work for real users. ClipFarm then uses **upload to inbox**: the clip arrives as a
draft in the TikTok app, and you (or the creator) tap to finish posting. The review
screen shows this clearly as "Draft in TikTok inbox, not public".

### Approval ⑤: Content Posting API audit (direct post)
Until audited, direct posts are limited to **private (SELF_ONLY)** visibility, from at
most 5 users per 24 h whose accounts must be private. The audit checks your posting
UI against TikTok's content-sharing guidelines (showing creator info, privacy choice,
consent). After approval, set `TIKTOK_DIRECT_POST_AUDITED=true`.

**Kids' content on TikTok:** TikTok requires users to be 13+ and has no made-for-kids
flag. Content made *for* under-13s may conflict with TikTok's terms. Check with the
creators, and ideally a lawyer (COPPA in the US, GDPR-K/AADC in the UK/EU), before
enabling TikTok for a creator. The same applies to Instagram.

## 5. Anthropic (step 3) ✅
Put your key in `ANTHROPIC_API_KEY`. It's used to rate transcript windows and, when
`SAFETY_VISION_ENABLED=true`, to scan sampled frames for unsafe scenes. Make sure
your creator agreements allow sending frames to a third-party processor.
