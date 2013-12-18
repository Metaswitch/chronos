require 'sinatra'

set :port, 1234

post '/callback' do 
  puts "Callback, data: #{request.body.read}, SeqNo: #{request.env["HTTP_X_SEQUENCE_NUMBER"]}"
end

put '/timers/:id' do
  puts "Replication, body: #{request.body.read}"
end
