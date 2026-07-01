# 🤖 Bronx Bot Manual

Welcome to the **Bronx Bot** documentation. Bronx is a multi-feature server management bot built on top of the **stoat++** library. It features MariaDB database configurations, customized command prefixes, dynamic welcome/leave actions, reaction roles with interactive conflict resolution, cleanups, and a highly customizable event logging engine.

---

## ⚙️ Core Configuration

Bronx loads settings from a `.env` file located in its working directory.

```ini
TOKEN=YOUR_BOT_TOKEN
DB_HOST=127.0.0.1
DB_PORT=3306
DB_USER=bronx
DB_PASSWORD=secret
DB_NAME=bronx
```

### Command Execution Notes
* **Prefixes**: Commands are triggered using the default prefix (`.`) or server-specific custom prefixes.
* **Case-Insensitivity**: Command name matching and aliases are case-insensitive.
* **Edit Dispatching**: If you edit a message that contained a command, Bronx will re-run the revised command parameters dynamically.

---

## 📂 Command Reference

### 1. General Cog (`General`)

Basic utilities and server information.

| Command | Usage | Description |
| :--- | :--- | :--- |
| `ping` | `.ping` | Checks bot response latency and API heartbeat. |
| `uptime` | `.uptime` | Displays launching relative timestamp and absolute runtime breakdown. |
| `say` | `.say <text...>` | Repeats text back to the channel. |

---

### 2. Moderation Cog (`Moderation`)

Basic administrative commands to enforce order.

| Command | Usage | Description |
| :--- | :--- | :--- |
| `ban` | `.ban <@user> [reason]` | Bans a user from the server with an optional reason. |
| `kick` | `.kick <@user>` | Kicks a member from the server. |
| `timeout` | `.timeout <@user> <duration>` | Times out a member for a duration (e.g. `10m`, `2h`, `1d`). |
| `cleanup` | `.cleanup [amount]` | Deletes bot responses and user invocations. Alias: `.cu`. |

#### ⚠️ Cleanup Pitfalls:
* **Rate Limits**: Avoid setting the cleanup amount too high (e.g., above 100). Revolt has aggressive message delete rate limits.
* **Cache Evictions**: Cleanup queries the last 100 messages. Messages older than that might not resolve their invocation pairings if they fall outside the immediate channel history cache.

---

### 3. Prefix Management (`Prefix`)

Configure custom trigger characters for your guild or channel.

* **`prefix list`**
  * Displays active prefixes.
* **`prefix add <guild|channel> <prefix>`**
  * Appends a prefix. Requires `ManageServer` permission or bot ownership.
* **`prefix remove <guild|channel> <prefix>`**
  * Removes a prefix. If all custom prefixes are cleared, Bronx reverts to default prefixes (`.`).

---

### 4. Reaction Roles (`ReactionRoles`)

Map message reactions to roles. Supports direct message targets, active replies, role name plaintext searches, and hierarchy conflict resolution.

* **`rr list`**
  * Lists active reaction role rules on the server.
* **`rr remove <message_id> <emoji>`**
  * Removes a reaction role mapping.
* **`rr add <target> <emoji> <role_name...>`**
  * Registers a reaction role.
  * **Target Options**:
    * A 26-character Revolt Message ID.
    * `^` to target the message directly above the command.
    * Active Reply: Reply to a message and invoke `.rr add ^ <emoji> <role>`.

#### ⚡ Interactive Plaintext Resolution
When specifying the role name, you can type it in plaintext (e.g. `Moderator Team`).
* If multiple roles match, Bronx sends an interactive prompt listing matching roles. Type the index number (e.g. `1`) within 30 seconds to select it. Bronx will clean up both the prompt and your selection immediately after confirmation.

#### ⚠️ Hierarchy Pitfall (HierarchyError)
If Bronx attempts to assign a role positioned **above** its highest role, the event fails. Bronx intercepts this and sends an audit warning:
> `⚠️ failed to assign role. Revolt requires the bot's role to be positioned above your top role. Please go to Server Settings -> Roles and drag my role above it.`

---

### 5. Welcome & Leave Messages (`WelcomeLeave`)

Greeting and departure systems with autorole support.

* **`.welcome setup` / `.leave setup`**
  * Launches the interactive welcome/leave wizard.
  * Configures channel, formatting type (text/embed), custom title, color, image, and body text.
* **`.welcome role <role>`**
  * Set a role assigned automatically to users on join.
* **`.welcome channel <#channel|none>`**
  * Re-routes greetings or disables them.
* **`.welcome type <text|embed>`**
  * Toggles greeting format.
* **`.welcome message <text...>`**
  * Sets raw greeting text (preserves whitespaces/newlines).
* **`.welcome edit <key:val...>`**
  * Edit individual embed fields (e.g., `title:Hello color:random image:url`).
* **`.welcome json <raw_json>`**
  * Directly import custom welcome embed structure.
* **`.welcome test`**
  * Simulates a member arrival (assigns the autorole and formats the greeting in the welcome channel).

#### 📝 Welcome/Leave Placeholders:
* `{user}`: Mentions the arriving/departing user (`<@ULID>`).
* `{username}`: Displays raw username.
* `{server}`: Name of the server.
* `{membercount}`: Total member count.

---

### 6. Event Logging (`Logging`)

Audit trail tracking edits, deletions, member joins/leaves, profile changes, and moderation commands.

* **`.log aio`**
  * **All-In-One automated setup**: Creates a `"Logging"` category and channels `#edits`, `#deletions`, `#joins-leaves`, and `#moderation`.
* **`.log channel <event> <#channel|none>`**
  * Binds an event category to a log channel.
* **`.log list`**
  * Lists active log targets.
* **`.log type <event> <text|embed>`**
  * Toggles log layout format.
* **`.log message <event> <text...>`**
  * Modifies log message body content (supporting newlines).
* **`.log edit <event> <key:val...>`**
  * Updates embed fields inline (`title`, `description`, `color`, `image`).
* **`.log json <event> <raw_json>`**
  * Directly imports raw JSON layout configs.

#### 📊 Event Placeholders Table

Ensure you include the required identifiers in custom message overrides to keep your log entries readable.

| Event Type | Subcommand Key | Supported Placeholders | Default Behavior |
| :--- | :--- | :--- | :--- |
| Message Deletions | `delete` | `{author}`, `{username}`, `{channel}`, `{content}` | Shows deleted text, author, and channel details. |
| Message Edits | `edit` | `{author}`, `{username}`, `{channel}`, `{before}`, `{after}` | Lists before and after states. |
| Member Joins | `join` | `{user}`, `{username}`, `{membercount}` | Audits arrival and updates total member count. |
| Member Leaves | `leave` | `{user}`, `{username}`, `{membercount}` | Audits departure and updates total member count. |
| Moderation Actions | `mod` | `{action}`, `{moderator}`, `{target}`, `{reason}`, `{details}` | Logs bans, kicks, and timeouts with reasons. |
| Profile Changes | `user` | `{user}`, `{oldUser}`, `{newUser}` | Triggers globally on username edits for server members. |

---

## 🛠️ Developer Implementation Details

### Message Caching
Revolt message update and deletion payloads from the gateway are partial—they do **not** contain message contents or authors.
To resolve edited and deleted content:
1. `LoggingModule` maintains a rolling, thread-safe message cache (`msg_cache_` capped at 5000 messages via `msg_ids_` deque tracking).
2. When a deletion occurs, Bronx pulls the deleted content from this local cache. If the message is not cached, it logs the event as `(not cached)`.

### Profile Changes (`on_user_update`)
Revolt's user updates are global events without a server context. When a member updates their username:
1. Bronx intercepts the event in a background thread.
2. It fetches all servers the bot is currently in.
3. For each server with `user_update` logging configured, it performs a REST check to see if the user is a member of that guild.
4. If they are, it formats the `{oldUser}` and `{newUser}` properties and posts the change to that guild's user log channel.

---

## ❓ Frequently Asked Questions (FAQ)

#### Q: Why are my deleted messages showing up as `(not cached)`?
**A**: If a message was sent before the bot was turned on, or if the message was sent too far back (exceeding the rolling 5,000 message buffer), it will not be in memory when deleted.

#### Q: How do I remove a log category or channel?
**A**: Run `.log channel <event_type> none` to stop tracking that log category. You can manually delete the channel category in your client.

#### Q: Can I use custom emojis in my welcome embeds?
**A**: Yes. Emojis in descriptions and fields are parsed correctly. If using custom server emojis, paste their raw ID or standard format.
