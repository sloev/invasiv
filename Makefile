build:
	docker build -t ofxdocker_2204_of_0_12_1_loaf .

bash:
	docker run --rm -it --entrypoint bash ofxdocker_2204_of_0_12_1_loaf

copy: build
	docker rm extract || true
	docker create --name extract ofxdocker_2204_of_0_12_1_loaf
	docker cp extract:/of/apps/myApps/loaf/bin ./artifacts
	docker rm extract

run:
	./artifacts/bin/loaf ./invasiv/main.lua

test: build
	docker rm extract || true
	docker create --name extract ofxdocker_2204_of_0_12_1_loaf
	docker cp extract:/of/apps/myApps/test/bin ./artifacts
	docker rm extract
	cd ./artifacts/bin/ && gdb -batch -ex "set pagination off" -ex "run" -ex "bt full" -ex "quit" --args test

run-test:
	cd ./testing && ../artifacts/bin/test

