if [ "$1" = "run" ]; then
    run=$1
    exfile=$2
else
    run=$2
    exfile=$1
fi

if [ "$exfile" = "" ]; then
    echo "Error: No example file was given"
    exit 1
fi

gcc -Wall $exfile ../src/*.c -I../src

if [ $? = 0 ]; then
    if [ "$run" != "" ]; then
        ./a.exe
    fi
fi
