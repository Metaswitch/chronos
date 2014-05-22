require 'sinatra'

set :port, 1234

post '/callback' do 
  puts "Callback:"
  puts "Data: #{request.body.read}"
  puts "SeqNo: #{request.env["HTTP_X_SEQUENCE_NUMBER"]}"
end

put '/timers/:id' do
  puts "Replication:"
  puts "Body: #{request.body.read}"
end
