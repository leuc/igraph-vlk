#!/usr/bin/env -S jq -rf
#
# Turns the Bluesky Jetstream firehose (one compact JSON event per line,
# straight off the websocket — Jetstream's own wire format is already
# newline-delimited JSON, no separate `jq -rMc .` pass needed) into NCOL
# edges describing the reply/comment relation between posts:
# "<child_post_uri> <parent_post_uri>" per reply, one line per event,
# suitable for streaming straight into igraph-vlk's stdin NCOL reader
# (see src/graph/stream.c).
#
# Only "app.bsky.feed.post" creation events that are themselves replies
# produce a line; original (non-reply) posts and other event kinds
# ("identity", "account") or operations ("update", "delete") are dropped.
#
# Usage (executable, via shebang):
#   curl -sN "wss://jetstream2.us-east.bsky.network/subscribe?wantedCollections=app.bsky.feed.post" \
#     | ./scripts/bsky_reply_graph.jq \
#     | ./build/igraph-vlk
#
# Or explicitly:
#   curl -sN "wss://jetstream2.us-east.bsky.network/subscribe?wantedCollections=app.bsky.feed.post" \
#     | jq --unbuffered -rf scripts/bsky_reply_graph.jq \
#     | ./build/igraph-vlk

select(.kind == "commit")
| select(.commit.operation == "create")
| select(.commit.collection == "app.bsky.feed.post")
| select(.commit.record.reply.parent.uri != null)
| ("at://" + .did + "/app.bsky.feed.post/" + .commit.rkey) as $post
| .commit.record.reply.parent.uri as $parent
| "\($post) \($parent)"
