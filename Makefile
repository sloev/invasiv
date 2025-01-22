build_base:
	podman build -t ofxdocker_2204_of_0_12_0_invasiv .
build:
	podman build -t ofxdocker_2204_of_0_12_0_invasiv_build -f invasiv.Dockerfile .
bash:
	podman run --rm -it    --entrypoint bash ofxdocker_2204_of_0_12_0_invasiv_build

copy: build
	podman create --name extract ofxdocker_2204_of_0_12_0_invasiv_build
	podman cp extract:/of/apps/myApps/invasiv/bin ./artifacts
	podman rm extract

run: copy
	./artifacts/bin/invasiv