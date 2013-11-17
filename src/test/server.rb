require 'sinatra'

set :port, 1234

post '/callback' do 
  puts "Recieved a callback"
end
