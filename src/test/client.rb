require "net/http"
require "json"

timer = { timing: { interval: 1000, "repeat-for" => 5000 },
          callback: { http: { uri: "localhost:1234/callback", opaque: "" } },
          reliability: { "replication-factor" => 1 } }

Net::HTTP.start("localhost", 7253) do |http|
  req = Net::HTTP::Post.new "http://localhost:7253/timers"
  req.body = JSON.generate(timer)

  rsp = http.request req
  puts rsp["Location"]
end
