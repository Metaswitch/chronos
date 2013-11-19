require 'sinatra'

set :port, 1234

post '/callback' do 
  puts "Callback, data: #{request.body.read}, SeqNo: #{request.env["HTTP_X_SEQUENCE_NUMBER"]}"
end
