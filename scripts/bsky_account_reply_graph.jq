#!/usr/bin/env -S jq -rf
#
# Turns the Bluesky Jetstream firehose into NCOL
# edges describing the reply/comment relation between *accounts* rather
# than posts: "<replier_did> <replied_to_did>" per reply, one line per
# event, suitable for streaming straight into igraph-vlk's stdin NCOL
# reader (see src/graph/stream.c). This is the account-level counterpart
# to scripts/bsky_reply_graph.jq, which keeps posts as the nodes.
#
# The parent post's author DID is recovered from its at:// URI
# ("at://<did>/app.bsky.feed.post/<rkey>") rather than looked up, since
# Jetstream events don't carry the parent author separately.
#
# Only "app.bsky.feed.post" creation events that are themselves replies
# to a different account produce a line; original (non-reply) posts,
# self-replies (replying within your own thread), and other event kinds
# ("identity", "account") or operations ("update", "delete") are dropped.
#
# Usage:
#   curl -sN "wss://jetstream2.us-east.bsky.network/subscribe?wantedCollections=app.bsky.feed.post" \
#     | ./scripts/bsky_account_reply_graph.jq \
#     | ./build/igraph-vlk

select(.kind == "commit")
| select(.commit.operation == "create")
| select(.commit.collection == "app.bsky.feed.post")
| select(.commit.record.reply.parent.uri != null)
| .did as $replier
| (.commit.record.reply.parent.uri | split("/"))[2] as $replied_to
| select($replier != $replied_to)
| "\($replier) \($replied_to)"
