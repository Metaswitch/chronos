require "net/http"
require "json"

timer = { timing: { interval: 1000, "repeat-for" => 5000 },
          callback: { http: { uri: "http://127.0.0.1:1234/callback", opaque: "Hello World!"} },
          reliability: { "replication-factor" => 2 } }

Net::HTTP.start("10.54.121.143", 1234) do |http|
  req = Net::HTTP::Post.new "http://localhost:1234/timers"
  req.body = JSON.generate(timer)
  rsp = http.request req
  puts rsp["Location"]
end
