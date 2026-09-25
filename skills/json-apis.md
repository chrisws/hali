# JSON APIs

Use this when the task is to pull live data from public JSON APIs — weather, news/headlines,
reference data, currency rates, etc. — via `TOOL:CURL`, typically to assemble into something
like a custom dashboard or news page. Pairs naturally with the `vanilla-web` skill for the
output side: fetch real data first, then render it, never the reverse.

## Picking APIs: prefer keyless, generous, and stable

A good default list, by category — all either fully keyless or usable with a public demo key,
which matters a lot for a "just works" experience with no signup step:

- **Weather**: Open-Meteo (`api.open-meteo.com`) — fully keyless, no rate-limit signup, needs
  lat/long rather than a city name (geocode first via its own geocoding endpoint if you only
  have a place name). `wttr.in/<location>?format=j1` is a keyless alternative that accepts
  plain city names directly.
- **News/tech/discussion**: Hacker News' Firebase-backed API (`hacker-news.firebaseio.com`) is
  fully keyless and gives top/new/best story IDs plus item details. Reddit's public JSON
  endpoints (append `.json` to any listing URL) are keyless but require a descriptive
  `User-Agent` header or requests get throttled/blocked.
- **Reference data**: REST Countries (`restcountries.com`) for country facts, Wikipedia's REST
  API (`en.wikipedia.org/api/rest_v1/...`) for summaries/"on this day," Open Library
  (`openlibrary.org`) for books — all keyless, all expect a `User-Agent` identifying the client.
- **Currency/finance**: Frankfurter (`api.frankfurter.app`) for exchange rates — keyless, no
  signup, ECB-sourced.
- **Space/misc for flavor**: Open Notify (`api.open-notify.org`) for ISS position, NASA's APOD
  endpoint works with the public `DEMO_KEY` (rate-limited but real) if you want a daily image.

Before relying on any API not in this list, `TOOL:CURL` it once and read the actual response —
don't assume a field structure from a similar-sounding API you remember; verify against what
this endpoint actually returns.

## Fetching with TOOL:CURL

- Use `-s` (silent) and check the actual HTTP status/response body for an error shape — most of
  these APIs return 200 with an error field, or a non-200 with a plain-text body, rather than a
  consistent error contract. Check both.
- Send a real `User-Agent` (e.g. `User-Agent: hali-dashboard/1.0 (contact info)`) for any API
  that requests one — Wikipedia and Reddit will throttle or reject generic/missing user agents.
- Don't hammer an endpoint in a tight loop. If you need several related calls (e.g. weather for
  five cities), space them or batch where the API supports it, and cache the result for the rest
  of the session rather than re-fetching on every render pass.
- If an API needs a key you don't have (most real news APIs — NewsAPI, GNews, etc. — require
  signup even on the free tier), say so plainly and either substitute a keyless alternative
  (Hacker News, Reddit, Wikipedia current-events feeds) or ask the user for a key rather than
  fabricating a response shaped like one.

## Parsing without guessing

- Read the actual JSON structure returned before writing code or template logic against it —
  field names, nesting, and types vary between APIs that superficially do "the same thing."
  Never write a parser against a remembered/assumed schema for an API you haven't just checked.
- Handle missing/null fields defensively — free public APIs are inconsistent about which fields
  are always present (a headline with no author, a weather response missing a forecast field
  for locations without full coverage).

## Assembling a page

- Fetch all the data first, confirm it looks sane, then hand it to the `vanilla-web` skill's
  rendering approach — real values baked into the HTML/JS, not placeholder content the user is
  expected to wire up later.
- Decide server-side snapshot vs. client-side fetch deliberately: a static page generated once
  from `TOOL:CURL` results is simplest and has no CORS concerns; having the page's own
  JavaScript fetch the APIs directly in-browser only works for endpoints that send permissive
  CORS headers (Open-Meteo and most of the list above do; verify any new API before relying on
  client-side fetch, since a blocked CORS request fails silently in ways that are confusing to
  debug from HTML alone).
- Design each section of the page to degrade gracefully on its own — if the weather call fails
  but the news call succeeds, show the news and a small "unavailable" note for weather, don't
  blank the whole page over one failed source.
- For anything meant to feel "live" (a news page, a dashboard), consider whether it needs a
  refresh mechanism (client-side polling, or documented as "regenerate by re-running") — decide
  and state which one rather than leaving it ambiguous.

## What NOT to do

- Don't fabricate API response data if a `TOOL:CURL` call fails or the API requires a key you
  don't have — say so and fall back to a keyless alternative or ask for the key.
- Don't parse against a guessed schema — always fetch and inspect the real response shape first.
- Don't hardcode a client-side API key in page JavaScript for any API that needs a real key —
  keyless APIs only for client-side fetch, or do the authenticated call server-side/build-time.
- Don't retry a failing endpoint in a tight loop hoping it recovers — back off and report the
  failure instead.
