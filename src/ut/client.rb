# @file client.rb
#
# Copyright (C) Metaswitch Networks 2017
# If license terms are provided to you in a COPYING file in the root directory
# of the source code repository by which you are accessing this code, then
# the license outlined in that COPYING file applies to your use.
# Otherwise no rights are granted except for those provided to you by
# Metaswitch Networks in a separate written agreement.

require "net/http"
require "json"

timer = { timing: { interval: 1, "repeat-for" => 5 },
          callback: { http: { uri: "http://127.0.0.1:1234/callback", opaque: "Hello World!"} },
          reliability: { "replication-factor" => 2 } }

Net::HTTP.start("10.48.154.121", 1234) do |http|
  req = Net::HTTP::Post.new "http://localhost:1234/timers"
  req.body = JSON.generate(timer)
  rsp = http.request req
  puts rsp["Location"]
end
