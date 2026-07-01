# bronx

server management bot, built on stoat++. mariadb config, custom prefixes, welcome/leave, reaction roles, cleanup, event logging.

---

## config

`.env` in the working directory:

```ini
TOKEN=YOUR_BOT_TOKEN
DB_HOST=127.0.0.1
DB_PORT=3306
DB_USER=bronx
DB_PASSWORD=secret
DB_NAME=bronx
```

default prefix `.`, custom per server. commands are case-insensitive. editing a command message re-runs it.

---

## commands

**general**
`ping` · `uptime` · `say <text>`

**moderation**
`ban <@user> [reason]`
`kick <@user>`
`timeout <@user> <duration>` — e.g. `10m`, `2h`, `1d`
`cleanup [amount]` (alias `.cu`) — stay under 100, only scans last 100 messages

**prefixes**
`prefix list`
`prefix add <guild|channel> <prefix>` — needs manageserver
`prefix remove <guild|channel> <prefix>` — clearing all resets to `.`

**reaction roles**
`rr list`
`rr remove <message_id> <emoji>`
`rr add <target> <emoji> <role>` — target: message id, `^`, or active reply

plaintext role names work; ambiguous matches trigger a numbered prompt (30s). if bronx's role is below the target role, it warns instead of failing silently.

**welcome / leave**
`.welcome setup` / `.leave setup` — wizard
`.welcome role <role>` — autorole
`.welcome channel <#channel|none>`
`.welcome type <text|embed>`
`.welcome message <text>`
`.welcome edit <key:val...>`
`.welcome json <raw_json>`
`.welcome test`

placeholders: `{user}` `{username}` `{server}` `{membercount}`

**logging**
`.log aio` — auto-creates category + #edits #deletions #joins-leaves #moderation
`.log channel <event> <#channel|none>`
`.log list`
`.log type <event> <text|embed>`
`.log message <event> <text>`
`.log edit <event> <key:val...>`
`.log json <event> <raw_json>`

| event | key | placeholders |
| --- | --- | --- |
| deletions | `delete` | `{author}` `{username}` `{channel}` `{content}` |
| edits | `edit` | `{author}` `{username}` `{channel}` `{before}` `{after}` |
| joins | `join` | `{user}` `{username}` `{membercount}` |
| leaves | `leave` | `{user}` `{username}` `{membercount}` |
| mod actions | `mod` | `{action}` `{moderator}` `{target}` `{reason}` `{details}` |
| profile changes | `user` | `{user}` `{oldUser}` `{newUser}` |

---

## internals

revolt's gateway payloads for edits/deletes are partial — no content or author. bronx keeps a rolling cache (`msg_cache_`, 5000 cap) and pulls from there; uncached messages log as `(not cached)`.

username changes are global events with no server context, so bronx checks membership per-server via rest before logging.

---

## faq

**`(not cached)` on deletes** — message predates bot startup or fell out of the 5000-message buffer.

**removing a log channel** — `.log channel <event> none`, then delete the channel yourself if wanted.

**custom emojis in embeds** — supported, raw id or standard format.