# @file server.rb
#
# Copyright (C) Metaswitch Networks 2016
# If license terms are provided to you in a COPYING file in the root directory
# of the source code repository by which you are accessing this code, then
# the license outlined in that COPYING file applies to your use.
# Otherwise no rights are granted except for those provided to you by
# Metaswitch Networks in a separate written agreement.

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
