#!/usr/bin/env ruby

=begin
	This file is part of Data Jockey.
	
	Data Jockey is free software: you can redistribute it and/or modify it
	under the terms of the GNU General Public License as published by the
	Free Software Foundation, either version 3 of the License, or (at your
	option) any later version.
	
	Data Jockey is distributed in the hope that it will be useful, but
	WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
	Public License for more details.
	
	You should have received a copy of the GNU General Public License along
	with Data Jockey.  If not, see <http://www.gnu.org/licenses/>.
=end

$: << ".."

require 'datajockey/base'
require 'datajockey/db'
require 'yaml'
require 'pathname'
require_relative 'getsamplerate'

#connect to the database
Datajockey::connect

include Datajockey

t = Tag.find_by_name("ends_off_beat")

#tag a work with ends_off_beat and put marker 5 before it starts to get bad
#NOTE markers are shown as + 1 in GUI, so in GUI it'll look like 6


t.audio_works.each do |w|
  puts "#{w.name} #{w.artist.name} #{w.annotation_file_location}"
  sr = getsamplerate(w.audio_file_location)
  frames = w.audio_file_seconds * sr
  puts "seconds: #{w.audio_file_seconds} frames #{frames}"

  start = w.jumps[5]
  if start
    start = start[:start]
    puts "got shit #{start}"
  else
    next
  end

  #copy the shit
  puts w.annotation_file_location
  f = Pathname.new(w.annotation_file_location)
  FileUtils.cp(w.annotation_file_location, File.join("/tmp/", f.basename))

  annotation = YAML.load(File.read(w.annotation_file_location))
  beats = annotation["beat_locations"]
  next unless beats
  beats = beats["frames"]
  next unless beats and beats.size > 1

  beats = beats[0..start]

  #find the tempo
  n = beats.size - 1
  dist = beats[0..n - 1].zip(beats[1..n]).collect { |t0, t1| t1 - t0 }.sort
  median = dist[dist.size / 2]
  if dist.size % 2 == 0
    median = (median + dist[dist.size / 2 + 1]).to_f / 2
  end
  puts median


  v = beats[-1] + median
  count = 0
  while v < frames
    count = count + 1
    beats << v
    v = (v + median).to_i
  end

  #smooth between points
  10.times do |t|
    if t % 2 == 0
      (beats.length - 2).downto(start).each do |i|
        n = ((beats[i + 1] - beats[i - 1]) / 2 + beats[i - 1]).to_i
        beats[i] = n
      end
    else
      (start..beats.length - 2).each do |i|
        n = ((beats[i + 1] - beats[i - 1]) / 2 + beats[i - 1]).to_i
        beats[i] = n
      end
    end
  end

  annotation["beat_locations"]["frames"] = beats
  File.open(w.annotation_file_location, "w") { |f|  f.print annotation.to_yaml }
  puts "#{w.name} done"
end

puts "DONE!"
