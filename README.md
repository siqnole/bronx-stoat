# bronx

Server management bot built on stoat++. Features MariaDB persistence, custom prefixes, welcome/leave actions, reaction roles, event logging, anti-raid protection, economy system, ticket management, counting game, AFK tracking, server statistics, role management, autoresponder, autoreact, and recurring messages.

---

## Configuration

Place a `.env` file in the working directory:

```ini
TOKEN=YOUR_BOT_TOKEN
DB_HOST=127.0.0.1
DB_PORT=3306
DB_USER=bronx
DB_PASSWORD=secret
DB_NAME=bronx
```

* **Command execution notes**: The default prefix is `.`. Pre-server custom prefixes are supported. Command names are case-insensitive. Editing a command message re-runs the updated parameters.

---

## Required Permissions by Default

The bot checks for appropriate user/bot permissions. The default permissions required to invoke commands (unless modified via the `.command` config system) are:

* **Administration & Configuration**:
  * `trust` / `command` — Requires being the Server Owner, Bot Owner, or having the `Manage Server` permission (or being registered on the server trust list).
  * `prefix add` / `prefix remove` — Requires `Manage Server` permission (or server trust list / owner).
* **Moderation**:
  * `ban` — Requires the executor and bot to have `Ban Members` permission, plus a valid hierarchy check.
  * `kick` — Requires `Kick Members` permission plus hierarchy check.
  * `timeout` — Requires `Timeout Members` permission plus hierarchy check.
  * `cleanup` / `pin` / `unpin` — Requires `Manage Messages` permission.
* **Welcome / Leave & Logs Setup**:
  * `welcome` / `leave` / `log` — Requires `Manage Server` permission.
* **Anti-Raid**:
  * `antiraid` — Requires `Manage Server` permission (or being the Server Owner / trusted user).
* **Reaction Roles**:
  * `rr` — Requires `Manage Roles` permission.
* **Roles**:
  * `role create` / `role edit` — Requires `Manage Roles` permission.
  * `role <users> <roles>` (batch assignment) — Requires `Manage Roles` permission with hierarchy enforcement.
* **Economy**:
  * `balance` / `beg` / `work` / `hustle` / `crime` / `search` / `leaderboard` — Available to all users.
  * `pay` / `deposit` / `withdraw` / `prestige` — Available to all users (self-service).
  * `rob` / `coinflip` / `slots` / `blackjack` / `highlow` — Available to all users (games/gambling/robbing).
* **Tickets**:
  * `ticket setup` / `ticket edit` / `ticket panel` / `ticket config` — Requires `Manage Server` permission.
  * `ticket list` — Requires `Manage Server` permission.
  * `ticket close` / `ticket add` / `ticket remove` — Requires `Manage Server` or being the ticket owner.
* **Counting**:
  * `count set` / `count number` / `count role` / `count passive` / `count config` / `count warning` — Requires `Manage Server` permission.
  * `count leaderboard` — Available to all users.
* **AFK**:
  * `afk` / `unafk` — Available to all users.
  * `afk setup` / `afk edit` — Requires `Manage Server` permission.
* **Stats**:
  * `stats` (all subcommands) — Available to all users.
* **Auto / Autoresponder / Autoreact / Recurring**:
  * `ar` / `are` / `recurse` — Requires `Manage Server` permission.
* **General / Utility**:
  * `ping` / `uptime` / `say` / `remind` / `reminders` / `emoji` / `ocr` / `dl` — Available to all users by default.
  * `stats` — Available to all users.

---

## Command Reference & Examples

### 1. General & Utility

| Command | Syntax | Example | Description |
| :--- | :--- | :--- | :--- |
| `help` | `.help` or `.h` | `.help` | Launches an interactive tabbed help menu with pagination and category navigation. |
| `ping` | `.ping` | `.ping` | Checks bot response latency and API heartbeat. |
| `uptime` | `.uptime` | `.uptime` | Displays relative launch time and absolute runtime. |
| `say` | `.say <text>` | `.say Hello world!` | Repeats text back to the channel. |
| `remind` | `.remind <duration> <msg>` | `.remind 2h30m check oven` | Sets a reminder. Durations support `s`, `m`, `h`, `d`, `w`, `mo`, `y`. |
| `reminders` | `.reminders` | `.reminders` | Lists all your active reminders. |

> **Deleted Channel Reminder Fallback**: If a reminder becomes due but the target channel was deleted, Bronx automatically creates a direct message channel and DMs the reminder to the user.

---

### 2. Moderation

| Command | Syntax | Example | Description |
| :--- | :--- | :--- | :--- |
| `ban` | `.ban <@user\|ID> [reason]` | `.ban @Syvor Spamming` | Bans a member from the server with an optional reason. |
| `kick` | `.kick <@user\|ID>` | `.kick 01KW4BF...` | Kicks a member from the server. |
| `timeout` | `.timeout <@user\|ID> <duration>` | `.timeout @user 1h` | Times out a member (e.g. `30m`, `2h`, `1d`). |
| `cleanup` | `.cleanup [amount]` (alias `.cu`) | `.cleanup 50` | Deletes bot responses and command invocations in the last 100 messages. |
| `pin` | `.pin [message_id]` | `.pin` (as reply) | Pins the replied message or the target message ID. |
| `unpin` | `.unpin [message_id]` | `.unpin 01KW...` | Unpins the replied message or the target message ID. |
| `modlogs` | `.modlogs <@user\|ID> [actions]` | `.modlogs @user` | View logs taken against a user, or actions they performed. |

---

### 3. Prefix Configuration

Configure custom prefix characters for a server or channel.

* **`prefix list`**
  * Displays active prefixes.
* **`prefix add <guild|channel> <prefix>`**
  * `.prefix add guild !` -> Adds `!` prefix server-wide.
  * `.prefix add channel $` -> Adds `$` prefix in the current channel only.
* **`prefix remove <guild|channel> <prefix>`**
  * `.prefix remove guild !` -> Removes prefix from server-wide list.

---

### 4. Custom Command Rules (`.command`)

The command config module allows you to allow/restrict commands or module groups to specific channels, roles, or users with specific permissions.

* **`trust <@user|@role|ID>`**
  * Toggle trusted status for a user/role. Trusted entities can run administrative commands without requiring explicit permissions.
  * *Example*: `.trust @Moderator` / `.trust 01KW...`
* **`command view <cmd/module>`**
  * View current restrictions configured for a command or module.
  * *Example*: `.command view ban`
* **`command allow <cmd/module> <#channel|@role|permission>`**
  * Whitelists a channel, role, or permission to run a command/module.
  * *Example*: `.command allow moderation #mod-spam`
  * *Example*: `.command allow say @Staff`
* **`command restrict <cmd/module> <#channel|@role|permission>`**
  * Blacklists a channel, role, or permission from running a command/module.
  * *Example*: `.command restrict ping #general`
* **`command disable <cmd/module>`**
  * Disable a command or module completely on the server.
  * *Example*: `.command disable say`
* **`command enable <cmd/module>`**
  * Re-enable a disabled command or module.
  * *Example*: `.command enable say`

---

### 5. Reaction Roles (`.rr`)

All subcommands require the **Manage Roles** permission. Users cannot add roles above their highest position, and dangerous roles (e.g., Manage Roles, Ban, Kick) require typing `yes` to confirm.

* **`rr list`**
  * Lists configured modals in the server.
* **`rr create <name>`**
  * `.rr create verified` -> Creates a blank reaction roles modal in the current channel.
* **`rr send <name> [#channel]`**
  * `.rr send verified #get-roles` -> Sends the modal and sets up all emoji reactions.
* **`rr add <target> <emoji> <role>`**
  * `.rr add verified 🌟 @Star` -> Adds the mapping to the `verified` modal.
* **`rr edit <name> <emoji> <emoji|role> <new_val>`**
  * `.rr edit verified 🌟 role @SuperStar` -> Edits role assigned by the emoji.
* **`rr parse <name> <msg_id|^>`**
  * `.rr parse colors ^` -> Scans the message above for emoji-role rules and imports them into a new modal.
* **`rr parse <msg_id|^>`**
  * `.rr parse 01KW...` -> Scans the message and activates the rules directly on it with a confirmation prompt.
* **`rr delete <name|msg_id>`**
  * `.rr delete verified` -> Deletes a modal template and removes reactions from all its instances.

---

### 6. Welcome & Leave Customization

Allows greeting and departure messages, along with automatic joining roles. Both text and rich JSON formats are supported.

* **`.welcome setup` / `.leave setup`**
  * Start the interactive wizard to set channel, layout format, embed titles, color, and message body.
* **`.welcome role <@role>`**
  * Set a role assigned to users automatically on arrival (autorole).
* **`.welcome channel <#channel|none>`**
  * Re-routes greetings or disables them.
* **`.welcome type <text|embed>`**
  * Toggles layout format.
* **`.welcome message <text...>`**
  * Sets raw greeting text. Supports formatting.
* **`.welcome json <raw_json>`**
  * Import a raw JSON description of the embed (see the JSON syntax guide below).
* **`.welcome edit <key:val...>`**
  * Inline edits fields. E.g. `.welcome edit title:Hello! color:#E74C3C`
* **`.welcome test`**
  * Simulates a user arrival to test the configuration.

#### Variables & Placeholders:
* `{user}`: Mentions the arriving/departing member (`<@ULID>`).
* `{username}`: Raw username of the member.
* `{server}`: Name of the server.
* `{membercount}`: Total members in the server.

---

### 7. Event Logging & Message Customization

Audit trail tracking for edits, deletes, joins, leaves, mod actions, and global user profile changes.

* **`.log aio`**
  * Auto-setup: Creates a `"Logging"` category and channels `#edits`, `#deletions`, `#joins-leaves`, and `#moderation`.
* **`.log channel <event> <#channel|none>`**
  * Binds an event category to a log channel. E.g. `.log channel delete #deletions`
* **`.log message <event> <text...>`**
  * Modifies log message body content.
* **`.log edit <event> <key:val...>`**
  * Updates embed fields inline (e.g. `.log edit delete title:Message Gone color:#FF0000`).
* **`.log json <event> <raw_json>`**
  * Directly import custom JSON layouts for events.

#### Event Keys & Variables:

| Event Type | Key | Placeholders | Default Behavior |
| :--- | :--- | :--- | :--- |
| Message Deletions | `delete` | `{author}`, `{username}`, `{channel}`, `{content}`, `{jump_url}` | Shows deleted text, author, and channel details. |
| Message Edits | `edit` | `{author}`, `{username}`, `{channel}`, `{before}`, `{after}`, `{jump_url}` | Lists before and after states. |
| Member Joins | `join` | `{user}`, `{username}`, `{membercount}` | Audits arrival and updates total member count. |
| Member Leaves | `leave` | `{user}`, `{username}`, `{membercount}` | Audits departure and updates total member count. |
| Moderation Actions | `mod` | `{action}`, `{moderator}`, `{target}`, `{reason}`, `{details}` | Logs bans, kicks, and timeouts with reasons. |
| Profile Changes | `user` | `{user}`, `{oldUser}`, `{newUser}` | Triggers globally on username edits for server members. |

---

### 8. Anti-Raid Protection (`.antiraid` / `.ar`)

Automated protection against join floods, message spam, and suspicious account profiles. Settings are server-scoped and persist in the database. Only the **server owner**, **bot owner**, or **trusted users / Manage Server** members may configure these rules.

#### Subcommands

* **`.antiraid`** / **`.antiraid status`**
  * Displays the current configuration for all anti-raid rules.
* **`.antiraid setup`**
  * Launches an interactive 5-step wizard to configure all rules at once.
* **`.antiraid join <count> <seconds> [action]`**
  * Configure join flood detection. Triggers when `<count>` or more users join within `<seconds>` seconds.
  * *Example*: `.antiraid join 5 60 kick` — kicks the triggering user when 5+ users join in 60 s.
* **`.antiraid spam <count> <seconds> [action]`**
  * Configure message spam detection. Triggers when a single user sends `<count>` messages within `<seconds>` seconds.
  * *Example*: `.antiraid spam 5 2 timeout` — times out users who send 5 messages in 2 s.
* **`.antiraid profile <action>`**
  * Set the action taken when a user joins with a default (no-avatar) profile.
  * *Example*: `.antiraid profile ban`
* **`.antiraid reset`**
  * Resets all thresholds and actions back to defaults (all actions: `none`).

#### Actions

| Action | Behaviour |
| :--- | :--- |
| `ban` | Permanently bans the user from the server. Reason includes the rule name. |
| `kick` | Removes the user from the server. |
| `timeout` | Applies a 10-minute server timeout (mute). |
| `warn` | Sends a DM warning to the user explaining which rule was triggered. |
| `none` | Logs the event (if a logging channel is configured) but takes no further action. |

#### Anti-Raid Event Logging

All triggered anti-raid events are dispatched through the standard logging system under the event key **`antiraid`**. During `.log aio` setup, antiraid events are automatically routed to the **#moderation** channel.

To manually route antiraid logs:
```
.log channel antiraid #your-log-channel
```

To customise the embed appearance:
```
.log edit antiraid title:🛡️ Raid Detected color:#D63031
```

#### Default Thresholds

| Rule | Default Threshold | Default Action |
| :--- | :--- | :--- |
| Join Flood | 5 joins / 60 s | `none` |
| Spam | 5 messages / 2 s | `none` |
| Default Profile | On any join with no avatar | `none` |

> **Note**: All rules default to `none` until explicitly configured — antiraid is passive until you set an action.

---

### 9. Custom Message Overrides (`.message`)

Configure the customized DM or public channel notification dispatched when a moderation action (ban, kick, timeout, warn) is executed.

* **`message list`**
  * Lists configured action notification templates.
* **`message show <action>`**
  * Displays template structure and format. E.g., `.message show ban`
* **`message type <action> <text|embed>`**
  * Toggles the message layout between standard text and embeds.
* **`message channel <action> <dm|public|none>`**
  * Configures target delivery. `dm` messages the user, `public` posts in the moderation channel, `none` disables notification.
* **`message set <action> <text...>`**
  * Overrides the template body. E.g., `.message set ban You were banned from {server} for: {reason}`
* **`message edit <action> <key:val...>`**
  * Inline edits fields. E.g. `.message edit ban title:Server Ban color:#D63031`
* **`message json <action> <raw_json>`**
  * Imports custom JSON templates (see syntax guide below).

#### Notification Variables:
* `{user}`: Mentions the target member.
* `{username}`: Raw username of the target.
* `{moderator}`: Mention of the executor.
* `{server}`: Name of the server.
* `{reason}`: Reason provided for the action.
* `{details}`: Action details (e.g. timeout duration).

---

## Embed JSON Import Guide

Commands accepting `<raw_json>` (`.welcome json`, `.leave json`, `.log json <event>`, and `.message json <action>`) parse standard JSON structures to build custom rich embeds. 

#### Valid Keys:
* `"title"`: String
* `"description"`: String (supports newlines and placeholders)
* `"url"`: String (navigational link)
* `"colour"` / `"color"`: Hex color string (e.g. `"#E74C3C"` or `"random"`)
* `"icon_url"`: String (avatar thumbnail url)
* `"image"`: String (large image attachment url)

#### JSON Example:
```json
{
  "title": "Welcome to {server}!",
  "description": "Glad you made it, {user}! You are our {membercount}th member.\nBe sure to check out <#01KW...>",
  "colour": "#3498DB",
  "image": "https://example.com/banner.png"
}
```
If the JSON is invalid, the bot will return a parsing error with line and character offsets.

---

### 10. Economy (`.balance`, `.work`, `.beg`, etc.)

A per-server coin economy with work cooldowns, side hustles, crimes, scavenging, robbery, gambling, a bank, and prestige system. All commands are available to every server member.

| Command | Syntax | Example | Description |
| :--- | :--- | :--- | :--- |
| `balance` | `.balance [@user]` | `.balance` | View your (or another user's) wallet, bank balance, networth, and prestige level. |
| `beg` | `.beg` | `.beg` | Beg for coins (45s cooldown). Payout is boosted by prestige multiplier. |
| `work` | `.work` | `.work` | Earn coins by working a shift (1h cooldown). Payout is boosted by prestige multiplier. |
| `hustle` | `.hustle` | `.hustle` | Earn coins by running a Bronx side hustle (30m cooldown). Payout is boosted by prestige multiplier. |
| `crime` | `.crime` (aliases `.rob-bank`, `.heist`) | `.crime` | Commit a street crime (1h cooldown). 60% success rate to win big; 40% chance to fail and get fined. Payout is boosted by prestige multiplier. |
| `search` | `.search` (aliases `.scavenge`, `.find`) | `.search` | Scavenge locations for loose change (10m cooldown). Payout is boosted by prestige multiplier. |
| `pay` | `.pay <@user\|ID> <amount>` | `.pay @siq 100` | Transfer coins from your wallet to another user. |
| `deposit` | `.deposit <amount\|all>` | `.deposit all` | Deposit coins from your wallet into your bank. |
| `withdraw` | `.withdraw <amount\|all>` | `.withdraw 500` | Withdraw coins from your bank into your wallet. |
| `rob` | `.rob <@user\|ID>` | `.rob @siq` | Attempt to steal coins from another user's wallet (2h cooldown). |
| `leaderboard` | `.leaderboard` (alias `.lb`) | `.leaderboard` | Show the top 10 richest users in the server. |
| `coinflip` | `.coinflip <amount> [heads\|tails]` | `.coinflip 100 heads` | Gamble coins on a coin flip. |
| `slots` | `.slots <amount>` | `.slots 50` | Spin the slot machine to win or lose coins. |
| `blackjack` | `.blackjack <amount>` (alias `.bj`) | `.blackjack 200` | Play a reaction-controlled blackjack game. |
| `highlow` | `.highlow <amount>` (alias `.hl`) | `.highlow 200` | Play an interactive high-low guessing game to double your bet. |
| `prestige` | `.prestige` | `.prestige` | Resets wallet and bank balance to 0 in exchange for +1 prestige level (needs `250,000 * (current_prestige + 1)` networth). Grants permanent +15% multiplier on all earnings commands and +50,000 bank capacity limit. |

> **Cooldowns**: Earning commands (`work`, `beg`, `hustle`, `crime`, `search`, `rob`) enforce per-user cooldowns managed by the cluster.

---

### 11. Tickets (`.ticket`)

Create and manage support ticket panels with category routing, staff access control, and per-ticket audit. Requires **Manage Server** for all administrative subcommands.

| Command | Syntax | Example | Description |
| :--- | :--- | :--- | :--- |
| `ticket setup` | `.ticket setup <name> [#category]` | `.ticket setup support` | Interactive wizard to create a new ticket panel. |
| `ticket edit` | `.ticket edit <name> <key:val...>` | `.ticket edit support title:Help Desk` | Inline-edit a panel's embed title, description, or color. |
| `ticket panel` | `.ticket panel <name> [#channel]` | `.ticket panel support #open-a-ticket` | Post the panel's button embed to a channel. |
| `ticket list` | `.ticket list` | `.ticket list` | List all configured ticket panels and active tickets. |
| `ticket close` | `.ticket close [reason]` | `.ticket close resolved` | Close the current ticket channel and archive it. |
| `ticket add` | `.ticket add <@user>` | `.ticket add @Moderator` | Add a user to the current ticket channel. |
| `ticket remove` | `.ticket remove <@user>` | `.ticket remove @user` | Remove a user from the current ticket channel. |
| `ticket config` | `.ticket config <name> <key:val...>` | `.ticket config support logs:#ticket-logs` | Update panel config (log channel, staff role, etc). |

---

### 12. Counting (`.count`)

A server counting game with leaderboards, warning messages, passive mode, and configurable reset roles. Administrative subcommands require **Manage Server**.

| Command | Syntax | Example | Description |
| :--- | :--- | :--- | :--- |
| `count set` | `.count set <#channel>` | `.count set #counting` | Set the counting channel for this server. |
| `count number` | `.count number <n>` | `.count number 500` | Manually set the current count value. |
| `count role` | `.count role <@role\|none>` | `.count role @Counter` | Assign a role to members who reach a milestone. |
| `count passive` | `.count passive <on\|off>` | `.count passive on` | Enable passive mode (bot deletes wrong numbers silently). |
| `count leaderboard` | `.count leaderboard` (alias `.count lb`) | `.count leaderboard` | Show top counters in the server. |
| `count config` | `.count config` | `.count config` | Display current counting configuration. |
| `count warning` | `.count warning <text...>` | `.count warning Wrong number! Try again.` | Set the message sent when a user counts incorrectly. |

---

### 13. AFK (`.afk`, `.unafk`)

Set an AFK status that auto-replies when you are mentioned. Supports custom messages, per-server setup, and staff-managed server-wide AFK configuration.

| Command | Syntax | Example | Description |
| :--- | :--- | :--- | :--- |
| `afk [message]` | `.afk [message]` | `.afk grabbing food` | Set yourself as AFK with an optional reason message. |
| `unafk` | `.unafk` | `.unafk` | Manually clear your AFK status. |
| `afk setup` | `.afk setup` | `.afk setup` | Interactive wizard to configure the server-wide AFK system (channel, embed, format). Requires **Manage Server**. |
| `afk edit` | `.afk edit <key:val...>` | `.afk edit title:Away color:#888` | Edit AFK embed appearance inline. Requires **Manage Server**. |
| `afk test` | `.afk test` | `.afk test` | Test the AFK reply embed with your current status. Requires **Manage Server**. |

> AFK status is automatically cleared when the AFK user sends a message in the server.

---

### 14. Stats (`.stats`)

Server telemetry covering message counts, active users, channels, and command usage. Available to all users. Supports text summaries and rendered charts (if compiled with Cairo).

| Command | Syntax | Example | Description |
| :--- | :--- | :--- | :--- |
| `stats` / `stats overview` | `.stats [timeframe]` | `.stats 30d` | Overview: total messages, active users, commands used. |
| `stats top` | `.stats top [timeframe]` | `.stats top 7d` | Top 10 members and channels by message count. |
| `stats top g` | `.stats top g [timeframe]` | `.stats top g` | Global leaderboard across all servers bronx is in. |
| `stats users` | `.stats users [timeframe]` | `.stats users 14d` | Active members bar chart (rendered image). |
| `stats channels` | `.stats channels [timeframe]` | `.stats channels` | Channel activity pie chart (rendered image). |
| `stats chart` | `.stats chart [line\|heatmap\|bars\|commands] [timeframe]` | `.stats chart heatmap 30d` | Render a custom chart. Chart types: `line`, `heatmap`, `bars`, `commands`. |

> **Timeframe formats**: `7d`, `2w`, `1m`, `1y`, or a plain integer (treated as days). Default is 7 days. Append `bots` to any subcommand to include bot accounts.

---

### 15. Role Management (`.role`)

Create, edit, and batch-assign roles. Requires **Manage Roles** permission. Role hierarchy is enforced — you cannot assign roles above your highest role.

| Command | Syntax | Example | Description |
| :--- | :--- | :--- | :--- |
| `role create` | `.role create <name [color/gradient], ...>` | `.role create VIP #ff0000` | Create one or more roles with optional solid colors or gradients. |
| `role edit` | `.role edit <role> <options>` | `.role edit VIP 135deg #ff0000 #0000ff` | Edit a role's name, color, or gradient angle. |
| `role <users> <roles>` | `.role <@u1,@u2> <role1,role2>` | `.role @siq VIP,Member` | Batch-toggle roles on one or more users. |
| `role * <role> [remove]` | `.role * <role> [remove]` | `.role * Member` | Assign or remove a role from every server member. |

> **Gradient support**: Roles accept `linear-gradient()` strings, multi-hex (auto-built gradient), or `deg:135` angle-only updates that preserve existing colors.

---

### 16. Autoresponder (`.ar`), Autoreact (`.are`), Recurring (`.recurse`)

Keyword-triggered responses and reactions, plus scheduled recurring messages. All require **Manage Server** permission. Up to 25 autoresponders and 25 autoreacts per server.

#### Autoresponder (`.ar`)

| Command | Syntax | Example | Description |
| :--- | :--- | :--- | :--- |
| `ar add` | `.ar add <name> <keyword>` | `.ar add rules read-the-rules` | Launch wizard to create a new autoresponder (3-step). |
| `ar remove` | `.ar remove <name\|id>` | `.ar remove rules` | Delete an autoresponder by name or numeric ID. |
| `ar list` | `.ar list` | `.ar list` | List all autoresponders (up to 25). |
| `ar edit` | `.ar edit <name\|id> <key:val...>` | `.ar edit rules content:Please read #rules` | Inline-edit keyword, match type, content, or embed fields. |
| `ar filter` | `.ar filter <name\|id> allow\|ignore role\|user\|channel <value>` | `.ar filter rules allow role @Member` | Scope the autoresponder to specific roles, users, or channels. |
| `ar info` | `.ar info <name\|id>` | `.ar info rules` | Show full details and current filter configuration. |

> **Match types**: `contains` (default), `exact`, `starts`, `ends`.

#### Autoreact (`.are`)

| Command | Syntax | Example | Description |
| :--- | :--- | :--- | :--- |
| `are add` | `.are add <name> <keyword> <emoji(s)>` | `.are add hype hype 🔥✨` | Create an autoreact rule (emojis: unicode, `:id:`, or comma-separated). |
| `are remove` | `.are remove <name\|id>` | `.are remove hype` | Delete an autoreact rule. |
| `are list` | `.are list` | `.are list` | List all autoreact rules. |
| `are edit` | `.are edit <name\|id> <key:val...>` | `.are edit hype emojis:🎉🔥` | Edit keyword, match type, or emoji set. |
| `are filter` | `.are filter <name\|id> allow\|ignore ...` | `.are filter hype ignore channel #general` | Scope the autoreact rule. |
| `are info` | `.are info <name\|id>` | `.are info hype` | Show full details and filter configuration. |

#### Recurring Messages (`.recurse`)

| Command | Syntax | Example | Description |
| :--- | :--- | :--- | :--- |
| `recurse add` | `.recurse add <name> interval:<duration> channel:<#ch>` | `.recurse add reminder interval:1h channel:#reminders` | Create a recurring message (wizard-based). |
| `recurse remove` | `.recurse remove <name\|id>` | `.recurse remove reminder` | Delete a recurring message. |
| `recurse list` | `.recurse list` | `.recurse list` | List all recurring messages. |
| `recurse edit` | `.recurse edit <name\|id> <key:val...>` | `.recurse edit reminder interval:30m` | Update interval, channel, content, or embed. |
| `recurse info` | `.recurse info <name\|id>` | `.recurse info reminder` | Show full recurring message details. |

---

### 17. Roblox Integration (`.roblox` / `.rbx`)

Manage Roblox account linking and view rich profile details.

| Command | Syntax | Example | Description |
| :--- | :--- | :--- | :--- |
| `roblox set` | `.roblox set <username>` | `.roblox set siqnole` | Interactive account linking verification wizard. |
| `roblox` | `.roblox [@user\|username]` | `.roblox @user` | Display a user's linked Roblox profile, or search by public username. Shows RAP, estimated value (Rolimons), socials (friends, followers, following), and 3D character renders. |

---

## Build & Run

### 1. Manual Build
To build the bot natively:
```bash
mkdir -p build
cd build
cmake ..
make -j$(nproc)
./bronx
```

### 2. Containerized Build & Run (Podman/Docker)
The bot can be containerized using a multi-stage Fedora build to package all libraries and OCR/cairo engines.

**Build the image:**
```bash
podman build -t bronx-bot .
```

**Run the container:**
To run the bot in host networking mode (allowing it to connect to your local MariaDB database at `127.0.0.1`):
```bash
podman run -d --name bronx-bot --net=host --restart on-failure bronx-bot
```

**View logs:**
```bash
podman logs -f bronx-bot
```